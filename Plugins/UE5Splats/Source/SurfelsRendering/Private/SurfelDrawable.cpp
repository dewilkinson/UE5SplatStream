// SurfelDrawable.cpp

#include "SurfelDrawable.h"

#include "SurfelShaders.h"
#include "CommonRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"

IMPLEMENT_GLOBAL_SHADER(FSurfelDiscAS, "/Plugin/UE5Splats/Private/SurfelsDisc.usf", "MainAS", SF_Amplification);
IMPLEMENT_GLOBAL_SHADER(FSurfelDiscMS, "/Plugin/UE5Splats/Private/SurfelsDisc.usf", "MainMS", SF_Mesh);
IMPLEMENT_GLOBAL_SHADER(FSurfelDiscVS, "/Plugin/UE5Splats/Private/SurfelsDisc.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FSurfelDiscPS, "/Plugin/UE5Splats/Private/SurfelsDisc.usf", "MainPS", SF_Pixel);

static TAutoConsoleVariable<int32> CVarSurfelsMeshShaders(
	TEXT("r.Surfels.MeshShaders"),
	1,
	TEXT("1 = draw surfels with amplification + mesh shaders wherever the GPU supports them (default).\n")
	TEXT("0 = always use the instanced vertex-shader fallback."),
	ECVF_RenderThreadSafe);

namespace
{
	TArray<FSurfelDrawable*> GRegisteredDrawables;

	FVector3f ToVector3f(const SurfelsCore::FVec3& V) { return FVector3f(V.X, V.Y, V.Z); }

	bool UseMeshShaders(const FViewInfo& View)
	{
		return CVarSurfelsMeshShaders.GetValueOnRenderThread() != 0
			&& GRHISupportsMeshShadersTier1
			&& RHISupportsMeshShadersTier1(View.GetShaderPlatform());
	}
}

FSurfelDrawable::FSurfelDrawable(FSurfelLevelPtr InLevel, const FSurfelDrawSettings& InSettings)
	: Level(MoveTemp(InLevel))
	, Settings(InSettings)
{
}

FSurfelDrawable::~FSurfelDrawable()
{
	check(!bRegistered);
}

void FSurfelDrawable::RegisterDrawable()
{
	check(IsInRenderingThread());
	if (!bRegistered && Level.IsValid() && !Level->Meshlets.empty())
	{
		GRegisteredDrawables.Add(this);
		bRegistered = true;
	}
}

void FSurfelDrawable::UnregisterDrawable()
{
	check(IsInRenderingThread());
	if (bRegistered)
	{
		GRegisteredDrawables.RemoveSingleSwap(this);
		bRegistered = false;
	}
	RecordsBuffer.SafeRelease();
	MeshletBuffer.SafeRelease();
}

const TArray<FSurfelDrawable*>& FSurfelDrawable::GetRegisteredDrawables()
{
	check(IsInRenderingThread());
	return GRegisteredDrawables;
}

uint32 FSurfelDrawable::GetGPUMemoryBytes() const
{
	return Level.IsValid() ? (uint32)(Level->Records.size() + Level->Meshlets.size() * sizeof(SurfelsCore::FMeshlet)) : 0;
}

void FSurfelDrawable::AddDiscPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth) const
{
	const SurfelsCore::FDecodedLevel& Data = *Level;
	const uint32 MeshletCount = (uint32)Data.Meshlets.size();

	// Upload once; the level's CPU copy outlives the upload because this drawable holds it.
	FRDGBufferRef Records;
	FRDGBufferRef Meshlets;
	if (!RecordsBuffer.IsValid())
	{
		const uint32 RecordWords = (uint32)(Data.Records.size() / sizeof(uint32));
		Records = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), RecordWords), TEXT("Surfels.Records"));
		GraphBuilder.QueueBufferUpload(Records, Data.Records.data(), Data.Records.size(), ERDGInitialDataFlags::NoCopy);
		RecordsBuffer = GraphBuilder.ConvertToExternalBuffer(Records);

		Meshlets = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(SurfelsCore::FMeshlet), MeshletCount), TEXT("Surfels.Meshlets"));
		GraphBuilder.QueueBufferUpload(Meshlets, Data.Meshlets.data(), Data.Meshlets.size() * sizeof(SurfelsCore::FMeshlet), ERDGInitialDataFlags::NoCopy);
		MeshletBuffer = GraphBuilder.ConvertToExternalBuffer(Meshlets);
	}
	else
	{
		Records = GraphBuilder.RegisterExternalBuffer(RecordsBuffer);
		Meshlets = GraphBuilder.RegisterExternalBuffer(MeshletBuffer);
	}

	// The shaders work in model space, exactly like surfels; only the matrices change. Going through
	// translated world space keeps large-world coordinates precise.
	const FMatrix ModelToWorld = GetModelToWorld();
	const FMatrix WorldToModel = ModelToWorld.Inverse();
	const FMatrix ModelToTranslatedWorld = ModelToWorld * FTranslationMatrix(View.ViewMatrices.GetPreViewTranslation());

	FSurfelDiscParameters* Parameters = GraphBuilder.AllocParameters<FSurfelDiscParameters>();
	Parameters->Records = GraphBuilder.CreateSRV(Records);
	Parameters->Meshlets = GraphBuilder.CreateSRV(Meshlets);
	Parameters->ModelToClip = FMatrix44f(ModelToTranslatedWorld * View.ViewMatrices.GetTranslatedWorldToClip());
	Parameters->CamRightModel = FVector3f(FVector(WorldToModel.TransformVector(View.GetViewRight())).GetSafeNormal());
	Parameters->CamUpModel = FVector3f(FVector(WorldToModel.TransformVector(View.GetViewUp())).GetSafeNormal());
	Parameters->EyeModel = FVector3f(FVector(WorldToModel.TransformPosition(View.ViewMatrices.GetViewOrigin())));
	Parameters->SplatScale = Settings.SplatScale;
	Parameters->LevelRadius = Data.PointSpacing * Settings.Coverage;
	// Clip w is view depth in world units (cm); the shaders' pixel-size rules want model units.
	Parameters->ClipWToModel = 1.0f / (float)FMath::Max(ModelToWorld.GetMaximumAxisScale(), UE_SMALL_NUMBER);
	Parameters->QuantMin = ToVector3f(Data.QuantMin);
	Parameters->QuantExtents = ToVector3f(Data.QuantExtents);
	Parameters->RecordFormat = (uint32)Data.Format;
	Parameters->WordsPerRecord = Data.RecordBytes / sizeof(uint32);
	Parameters->MeshletCount = MeshletCount;
	Parameters->LightDirModel = FVector3f(0.5f, 0.8f, 0.6f).GetSafeNormal(); // surfels' fixed key light
	Parameters->OrientMode = (uint32)Settings.Orient;
	Parameters->ScaleLog2Min = Data.ScaleLog2Min;
	Parameters->ScaleLog2Max = Data.ScaleLog2Max;
	Parameters->PreExposure = View.PreExposure;
	Parameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FSurfelDiscPS> PixelShader(ShaderMap);
	const FIntRect ViewRect = View.ViewRect;

	// Shader map refs for the mesh stages exist only where they were compiled, so build them inside the branch.
	if (UseMeshShaders(View))
	{
#if PLATFORM_SUPPORTS_MESH_SHADERS
		TShaderMapRef<FSurfelDiscAS> AmplificationShader(ShaderMap);
		TShaderMapRef<FSurfelDiscMS> MeshShader(ShaderMap);
		const uint32 GroupCount = FMath::DivideAndRoundUp(MeshletCount, GSurfelMeshletsPerASGroup);
		const uint32 GroupsX = FMath::Min(GroupCount, 65535u);
		const uint32 GroupsY = FMath::DivideAndRoundUp(GroupCount, GroupsX);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Surfels.Discs (mesh shaders, %u meshlets)", MeshletCount),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, AmplificationShader, MeshShader, PixelShader, ViewRect, GroupsX, GroupsY](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

				FGraphicsPipelineStateInitializer PSOInit;
				RHICmdList.ApplyCachedRenderTargets(PSOInit);
				PSOInit.BlendState = TStaticBlendState<>::GetRHI();
				PSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				PSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
				PSOInit.PrimitiveType = PT_TriangleList;
				PSOInit.BoundShaderState.SetAmplificationShader(AmplificationShader.GetAmplificationShader());
				PSOInit.BoundShaderState.SetMeshShader(MeshShader.GetMeshShader());
				PSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, PSOInit, 0);

				SetShaderParameters(RHICmdList, AmplificationShader, AmplificationShader.GetAmplificationShader(), *Parameters);
				SetShaderParameters(RHICmdList, MeshShader, MeshShader.GetMeshShader(), *Parameters);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
				RHICmdList.DispatchMeshShader(GroupsX, GroupsY, 1);
			});
		return;
#endif
	}

	TShaderMapRef<FSurfelDiscVS> VertexShader(ShaderMap);
	const uint32 InstanceCount = MeshletCount * GSurfelsPerMeshlet;
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Surfels.Discs (vertex fallback, %u meshlets)", MeshletCount),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, ViewRect, InstanceCount](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

			FGraphicsPipelineStateInitializer PSOInit;
			RHICmdList.ApplyCachedRenderTargets(PSOInit);
			PSOInit.BlendState = TStaticBlendState<>::GetRHI();
			PSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			PSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
			PSOInit.PrimitiveType = PT_TriangleList;
			PSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			PSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			PSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			SetGraphicsPipelineState(RHICmdList, PSOInit, 0);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *Parameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
			RHICmdList.DrawPrimitive(0, 2, InstanceCount); // Six vertices (two triangles) per surfel
		});
}
