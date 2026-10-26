// SplatDrawable.cpp

#include "SplatDrawable.h"

#include "SplatShaders.h"
#include "CommonRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"

IMPLEMENT_GLOBAL_SHADER(FSplatDiscAS, "/Plugin/SplatPak/Private/SplatDisc.usf", "MainAS", SF_Amplification);
IMPLEMENT_GLOBAL_SHADER(FSplatDiscMS, "/Plugin/SplatPak/Private/SplatDisc.usf", "MainMS", SF_Mesh);
IMPLEMENT_GLOBAL_SHADER(FSplatDiscVS, "/Plugin/SplatPak/Private/SplatDisc.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FSplatDiscPS, "/Plugin/SplatPak/Private/SplatDisc.usf", "MainPS", SF_Pixel);

static TAutoConsoleVariable<int32> CVarSplatPakMeshShaders(
	TEXT("r.SplatPak.MeshShaders"),
	1,
	TEXT("1 = draw splats with amplification + mesh shaders wherever the GPU supports them (default).\n")
	TEXT("0 = always use the instanced vertex-shader fallback."),
	ECVF_RenderThreadSafe);

DEFINE_LOG_CATEGORY_STATIC(LogSplatPakRendering, Log, All);

namespace
{
	TArray<FSplatDrawable*> GRegisteredDrawables;

	FVector3f ToVector3f(const SplatCore::FVec3& V) { return FVector3f(V.X, V.Y, V.Z); }

	bool UseMeshShaders(const FViewInfo& View)
	{
		const bool bRequested = CVarSplatPakMeshShaders.GetValueOnRenderThread() != 0;
		const bool bGPU = GRHISupportsMeshShadersTier1 != 0;
		const bool bPlatform = RHISupportsMeshShadersTier1(View.GetShaderPlatform());
		const bool bUse = bRequested && bGPU && bPlatform;

		// Log the path whenever it changes, so captures and bug reports say which one drew.
		static int32 LastPath = -1;
		if (LastPath != (int32)bUse)
		{
			LastPath = (int32)bUse;
			UE_LOG(LogSplatPakRendering, Log, TEXT("Drawing splats with %s (r.SplatPak.MeshShaders=%d, GPU Mesh Shader Tier 1=%d, platform=%d)"),
				bUse ? TEXT("amplification + mesh shaders") : TEXT("the vertex-shader fallback"), bRequested ? 1 : 0, bGPU ? 1 : 0, bPlatform ? 1 : 0);
		}
		return bUse;
	}
}

FSplatDrawable::FSplatDrawable(FSplatLevelPtr InLevel, const FSplatDrawSettings& InSettings)
	: Level(MoveTemp(InLevel))
	, Settings(InSettings)
{
}

FSplatDrawable::~FSplatDrawable()
{
	check(!bRegistered);
}

void FSplatDrawable::RegisterDrawable()
{
	check(IsInRenderingThread());
	if (!bRegistered && Level.IsValid() && !Level->Meshlets.empty())
	{
		GRegisteredDrawables.Add(this);
		bRegistered = true;
	}
}

void FSplatDrawable::UnregisterDrawable()
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

const TArray<FSplatDrawable*>& FSplatDrawable::GetRegisteredDrawables()
{
	check(IsInRenderingThread());
	return GRegisteredDrawables;
}

uint32 FSplatDrawable::GetGPUMemoryBytes() const
{
	return Level.IsValid() ? (uint32)(Level->Records.size() + Level->Meshlets.size() * sizeof(SplatCore::FMeshlet)) : 0;
}

void FSplatDrawable::AddDiscPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth) const
{
	const SplatCore::FDecodedLevel& Data = *Level;
	const uint32 MeshletCount = (uint32)Data.Meshlets.size();

	// Upload once; the level's CPU copy outlives the upload because this drawable holds it.
	FRDGBufferRef Records;
	FRDGBufferRef Meshlets;
	if (!RecordsBuffer.IsValid())
	{
		const uint32 RecordWords = (uint32)(Data.Records.size() / sizeof(uint32));
		Records = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), RecordWords), TEXT("SplatPak.Records"));
		GraphBuilder.QueueBufferUpload(Records, Data.Records.data(), Data.Records.size(), ERDGInitialDataFlags::NoCopy);
		RecordsBuffer = GraphBuilder.ConvertToExternalBuffer(Records);

		Meshlets = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(SplatCore::FMeshlet), MeshletCount), TEXT("SplatPak.Meshlets"));
		GraphBuilder.QueueBufferUpload(Meshlets, Data.Meshlets.data(), Data.Meshlets.size() * sizeof(SplatCore::FMeshlet), ERDGInitialDataFlags::NoCopy);
		MeshletBuffer = GraphBuilder.ConvertToExternalBuffer(Meshlets);
	}
	else
	{
		Records = GraphBuilder.RegisterExternalBuffer(RecordsBuffer);
		Meshlets = GraphBuilder.RegisterExternalBuffer(MeshletBuffer);
	}

	// The shaders work in model space, exactly like SplatPak; only the matrices change. Going through
	// translated world space keeps large-world coordinates precise.
	const FMatrix ModelToWorld = GetModelToWorld();
	const FMatrix WorldToModel = ModelToWorld.Inverse();
	const FMatrix ModelToTranslatedWorld = ModelToWorld * FTranslationMatrix(View.ViewMatrices.GetPreViewTranslation());

	FSplatDiscParameters* Parameters = GraphBuilder.AllocParameters<FSplatDiscParameters>();
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
	Parameters->LightDirModel = FVector3f(0.5f, 0.8f, 0.6f).GetSafeNormal(); // SplatPak's fixed key light
	Parameters->OrientMode = (uint32)Settings.Orient;
	Parameters->ScaleLog2Min = Data.ScaleLog2Min;
	Parameters->ScaleLog2Max = Data.ScaleLog2Max;
	Parameters->PreExposure = View.PreExposure;
	Parameters->EmissiveIntensity = Settings.EmissiveIntensity;
	Parameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FSplatDiscPS> PixelShader(ShaderMap);
	const FIntRect ViewRect = View.ViewRect;

	// Shader map refs for the mesh stages exist only where they were compiled, so build them inside the branch.
	if (UseMeshShaders(View))
	{
#if PLATFORM_SUPPORTS_MESH_SHADERS
		TShaderMapRef<FSplatDiscAS> AmplificationShader(ShaderMap);
		TShaderMapRef<FSplatDiscMS> MeshShader(ShaderMap);
		const uint32 GroupCount = FMath::DivideAndRoundUp(MeshletCount, GSplatMeshletsPerASGroup);
		const uint32 GroupsX = FMath::Min(GroupCount, 65535u);
		const uint32 GroupsY = FMath::DivideAndRoundUp(GroupCount, GroupsX);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SplatPak.Discs (mesh shaders, %u meshlets)", MeshletCount),
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

	TShaderMapRef<FSplatDiscVS> VertexShader(ShaderMap);
	const uint32 InstanceCount = MeshletCount * GSplatsPerMeshlet;
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("SplatPak.Discs (vertex fallback, %u meshlets)", MeshletCount),
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
			RHICmdList.DrawPrimitive(0, 2, InstanceCount); // Six vertices (two triangles) per splat
		});
}
