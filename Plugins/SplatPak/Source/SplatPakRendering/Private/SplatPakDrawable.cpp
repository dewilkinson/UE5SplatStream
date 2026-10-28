// SplatPakDrawable.cpp

#include "SplatPakDrawable.h"

#include "SplatPakShaders.h"
#include "CommonRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "SceneRendering.h"
#include "SystemTextures.h"

IMPLEMENT_GLOBAL_SHADER(FSplatPakDiscAS, "/Plugin/SplatPak/Private/SplatPakDisc.usf", "MainAS", SF_Amplification);
IMPLEMENT_GLOBAL_SHADER(FSplatPakDiscMS, "/Plugin/SplatPak/Private/SplatPakDisc.usf", "MainMS", SF_Mesh);
IMPLEMENT_GLOBAL_SHADER(FSplatPakDiscVS, "/Plugin/SplatPak/Private/SplatPakDisc.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FSplatPakDiscPS, "/Plugin/SplatPak/Private/SplatPakDisc.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FSplatPakDiscGBufferPS, "/Plugin/SplatPak/Private/SplatPakDisc.usf", "MainGBufferPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FSplatPakGaussianExpandCS, "/Plugin/SplatPak/Private/SplatPakGaussian.usf", "ExpandCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSplatPakGaussianHistogramCS, "/Plugin/SplatPak/Private/SplatPakGaussian.usf", "HistogramCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSplatPakGaussianScanCS, "/Plugin/SplatPak/Private/SplatPakGaussian.usf", "ScanCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSplatPakGaussianScatterCS, "/Plugin/SplatPak/Private/SplatPakGaussian.usf", "ScatterCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSplatPakGaussianVS, "/Plugin/SplatPak/Private/SplatPakGaussian.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FSplatPakGaussianPS, "/Plugin/SplatPak/Private/SplatPakGaussian.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FSplatPakMeshVS, "/Plugin/SplatPak/Private/SplatPakMesh.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FSplatPakMeshPS, "/Plugin/SplatPak/Private/SplatPakMesh.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FSplatPakMeshNormalVS, "/Plugin/SplatPak/Private/SplatPakMesh.usf", "MainNormalVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FSplatPakMeshNormalPS, "/Plugin/SplatPak/Private/SplatPakMesh.usf", "MainNormalPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FSplatPakOccRasterCS, "/Plugin/SplatPak/Private/SplatPakOcclusion.usf", "OccRasterCS", SF_Compute);

static TAutoConsoleVariable<int32> CVarSplatPakMeshShaders(
	TEXT("r.SplatPak.MeshShaders"),
	1,
	TEXT("1 = draw splats with amplification + mesh shaders wherever the GPU supports them (default).\n")
	TEXT("0 = always use the instanced vertex-shader fallback."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSplatPakOcclusionCulling(
	TEXT("r.SplatPak.OcclusionCulling"),
	1,
	TEXT("1 = skip meshlets hidden behind the package's occluder mesh (mesh-shader path; default).\n")
	TEXT("0 = draw every meshlet in the frustum and leave occlusion to the depth test.\n")
	TEXT("2 = debug: draw only the meshlets occlusion culling skips."),
	ECVF_RenderThreadSafe);

// Width of the occlusion grid in pixels; the height follows the view's aspect ratio.
static constexpr uint32 GSplatOcclusionGridWidth = 256;

DEFINE_LOG_CATEGORY_STATIC(LogSplatPakRendering, Log, All);

namespace
{
	TArray<FSplatPakDrawable*> GRegisteredDrawables;

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

FSplatPakDrawable::FSplatPakDrawable(FSplatPakLevelPtr InLevel, const FSplatPakDrawSettings& InSettings)
	: Level(MoveTemp(InLevel))
	, Settings(InSettings)
{
}

FSplatPakDrawable::~FSplatPakDrawable()
{
	check(!bRegistered);
}

void FSplatPakDrawable::RegisterDrawable()
{
	check(IsInRenderingThread());
	if (!bRegistered && Level.IsValid() && !Level->Meshlets.empty())
	{
		GRegisteredDrawables.Add(this);
		bRegistered = true;
	}
}

void FSplatPakDrawable::UnregisterDrawable()
{
	check(IsInRenderingThread());
	if (bRegistered)
	{
		GRegisteredDrawables.RemoveSingleSwap(this);
		bRegistered = false;
	}
	RecordsBuffer.SafeRelease();
	MeshletBuffer.SafeRelease();
	OccluderPositionBuffer.SafeRelease();
	OccluderIndexBuffer.SafeRelease();
	StreamRecordsBuffer.SafeRelease();
	OccluderColourBuffer.SafeRelease();
	ShadowPositionBuffer.SafeRelease();
	ShadowColourBuffer.SafeRelease();
	ShadowIndexBuffer.SafeRelease();
	ShadowNormalBuffer.SafeRelease();
}

void FSplatPakDrawable::SetStreamSource_RenderThread(FSplatPakStreamSourcePtr Source)
{
	check(IsInRenderingThread());
	Stream = MoveTemp(Source);
	StreamMeshlets.Reset();
	StreamRecordsBuffer.SafeRelease();
}

const TArray<FSplatPakDrawable*>& FSplatPakDrawable::GetRegisteredDrawables()
{
	check(IsInRenderingThread());
	return GRegisteredDrawables;
}

uint32 FSplatPakDrawable::GetGPUMemoryBytes() const
{
	const uint32 StreamBytes = Stream.IsValid() ? (uint32)(Stream->RecordWords.size() * sizeof(uint32) + StreamMeshlets.Num() * sizeof(SplatCore::FMeshlet)) : 0;
	return StreamBytes + (Level.IsValid() ? (uint32)(Level->Records.size() + Level->Meshlets.size() * sizeof(SplatCore::FMeshlet)
		+ Level->OccluderPositions.size() * sizeof(SplatCore::FVec3) + Level->OccluderIndices.size() * sizeof(uint32)) : 0);
}

bool FSplatPakDrawable::DrawsLit(const FSceneView& View) const
{
	// Substrate replaces the GBuffer this pass writes; forward shading has none
	return Settings.bLit && !Substrate::IsSubstrateEnabled() && !IsForwardShadingEnabled(View.GetShaderPlatform());
}

void FSplatPakDrawable::AddDiscPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth) const
{
	AddDiscs(GraphBuilder, View, SceneColor, SceneDepth, nullptr);
}

void FSplatPakDrawable::AddDiscGBufferPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FRenderTargetBindingSlots& BasePassTargets) const
{
	FRDGTextureRef SceneColor = BasePassTargets.Output[0].GetTexture();
	FRDGTextureRef SceneDepth = BasePassTargets.DepthStencil.GetTexture();
	if (SceneColor && SceneDepth && BasePassTargets.Output[3].GetTexture())
	{
		AddDiscs(GraphBuilder, View, SceneColor, SceneDepth, &BasePassTargets);
	}
}

// The disc draw for both passes. GBufferTargets null: the unlit pass into SceneColor (after lighting); otherwise lit mode,
// into the base pass's scene colour and GBufferA-C, without the gap fill or the Gaussians.
void FSplatPakDrawable::AddDiscs(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth,
	const FRenderTargetBindingSlots* GBufferTargets) const
{
	const SplatCore::FDecodedLevel& Data = *Level;
	// Streaming: the frame's render list over every level's records; otherwise the resident level. Both share the
	// package's quantisation frame (Data), which every level's records are written in.
	const bool bStreaming = Stream.IsValid() && StreamMeshlets.Num() > 0;
	const uint32 MeshletCount = bStreaming ? (uint32)StreamMeshlets.Num() : (uint32)Data.Meshlets.size();
	if (MeshletCount == 0)
	{
		return;
	}

	// Upload once; the level's CPU copy outlives the upload because this drawable holds it.
	FRDGBufferRef Records;
	FRDGBufferRef Meshlets;
	if (bStreaming)
	{
		if (!StreamRecordsBuffer.IsValid())
		{
			Records = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), (uint32)Stream->RecordWords.size()), TEXT("SplatPak.StreamRecords"));
			GraphBuilder.QueueBufferUpload(Records, Stream->RecordWords.data(), Stream->RecordWords.size() * sizeof(uint32), ERDGInitialDataFlags::NoCopy);
			StreamRecordsBuffer = GraphBuilder.ConvertToExternalBuffer(Records);
		}
		else
		{
			Records = GraphBuilder.RegisterExternalBuffer(StreamRecordsBuffer);
		}
		// The render list changes every frame: copied into the graph's own upload
		Meshlets = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(SplatCore::FMeshlet), MeshletCount), TEXT("SplatPak.StreamMeshlets"));
		GraphBuilder.QueueBufferUpload(Meshlets, StreamMeshlets.GetData(), MeshletCount * sizeof(SplatCore::FMeshlet));
	}
	else if (!RecordsBuffer.IsValid())
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

	FSplatPakDiscParameters* Parameters = GraphBuilder.AllocParameters<FSplatPakDiscParameters>();
	Parameters->Records = GraphBuilder.CreateSRV(Records);
	Parameters->Meshlets = GraphBuilder.CreateSRV(Meshlets);
	Parameters->ModelToClip = FMatrix44f(ModelToTranslatedWorld * View.ViewMatrices.GetTranslatedWorldToClip());
	Parameters->CamRightModel = FVector3f(FVector(WorldToModel.TransformVector(View.GetViewRight())).GetSafeNormal());
	Parameters->CamUpModel = FVector3f(FVector(WorldToModel.TransformVector(View.GetViewUp())).GetSafeNormal());
	Parameters->EyeModel = FVector3f(FVector(WorldToModel.TransformPosition(View.ViewMatrices.GetViewOrigin())));
	Parameters->SplatScale = Settings.SplatScale;
	// Disc radius per level: the level's spacing x coverage (streaming), or the resident level's for every slot
	float MaxLevelRadius = 0.0f;
	for (int32 L = 0; L < 8; L++)
	{
		const float Spacing = bStreaming ? Stream->LevelSpacing[L] : Data.PointSpacing;
		const float Radius = Spacing * Settings.Coverage;
		Parameters->LevelRadii[L >> 2][L & 3] = Radius;
		MaxLevelRadius = FMath::Max(MaxLevelRadius, Radius);
	}
	Parameters->GlowIntensity = Settings.GlowIntensity;
	Parameters->GlowHue = Settings.GlowHue;
	Parameters->StreamFlags = bStreaming ? ((Settings.bDitheredTransitions ? 1u : 0u) | (Settings.bRefinementVisualizer ? 2u : 0u)) : 0u;
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
	const bool bGBuffer = GBufferTargets != nullptr;
	Parameters->ModelToWorldNormal = FMatrix44f(ModelToWorld.RemoveTranslation().Inverse().GetTransposed());
	Parameters->LitMode = bGBuffer ? 1u : 0u;
	Parameters->LitBlend = FMath::Clamp(Settings.LitBlend, 0.0f, 1.0f);
	Parameters->LitRoughness = FMath::Clamp(Settings.LitRoughness, 0.0f, 1.0f);
	Parameters->LitSpecular = FMath::Clamp(Settings.LitSpecular, 0.0f, 1.0f);
	Parameters->AlbedoScale = FMath::Max(Settings.AlbedoScale, 0.0f);
	// Normal smoothing (lit mode): the shadow mesh's normals where a disc lies within 3% of the model's size of its surface
	FRDGTextureRef MeshNormals = (bGBuffer && Settings.NormalSmoothing > 0.0f)
		? AddMeshNormalPass(GraphBuilder, View, Parameters->ModelToClip, Parameters->ModelToWorldNormal, SceneDepth) : nullptr;
	Parameters->MeshNormalTexture = MeshNormals ? MeshNormals : GSystemTextures.GetBlackDummy(GraphBuilder);
	Parameters->NormalSmoothing = MeshNormals ? FMath::Clamp(Settings.NormalSmoothing, 0.0f, 1.0f) : 0.0f;
	Parameters->MeshNormalTolerance = 0.03f * FMath::Max3(Data.QuantExtents.X, Data.QuantExtents.Y, Data.QuantExtents.Z) * (float)ModelToWorld.GetMaximumAxisScale();

	// Occlusion culling (mesh-shader path): the occluder mesh rasterised from the view into a small depth grid,
	// which the amplification shader tests each meshlet against (SplatPakOcclusion.ush)
	const bool bMeshShaders = UseMeshShaders(View);
	const uint32 OccTriCount = (uint32)(Data.OccluderIndices.size() / 3);
	const int32 OcclusionMode = FMath::Clamp(CVarSplatPakOcclusionCulling.GetValueOnRenderThread(), 0, 2);
	const bool bOcclusion = bMeshShaders && OccTriCount > 0 && OcclusionMode != 0;
	const uint32 OccWidth = bOcclusion ? GSplatOcclusionGridWidth : 1u;
	const uint32 OccHeight = bOcclusion ? (uint32)FMath::Clamp(FMath::RoundToInt((float)GSplatOcclusionGridWidth * View.ViewRect.Height() / FMath::Max(1, View.ViewRect.Width())), 16, 1024) : 1u;
	FRDGBufferRef OccDepth = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), OccWidth * OccHeight), TEXT("SplatPak.OcclusionGrid"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OccDepth), 0x7F7FFFFFu); // FLT_MAX bits: no occluder
	if (bOcclusion)
	{
		FRDGBufferRef OccPositions;
		FRDGBufferRef OccIndices;
		if (!OccluderPositionBuffer.IsValid())
		{
			const uint32 Floats = (uint32)Data.OccluderPositions.size() * 3;
			OccPositions = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), Floats), TEXT("SplatPak.OccluderPositions"));
			GraphBuilder.QueueBufferUpload(OccPositions, Data.OccluderPositions.data(), Floats * sizeof(float), ERDGInitialDataFlags::NoCopy);
			OccluderPositionBuffer = GraphBuilder.ConvertToExternalBuffer(OccPositions);
			OccIndices = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), (uint32)Data.OccluderIndices.size()), TEXT("SplatPak.OccluderIndices"));
			GraphBuilder.QueueBufferUpload(OccIndices, Data.OccluderIndices.data(), Data.OccluderIndices.size() * sizeof(uint32), ERDGInitialDataFlags::NoCopy);
			OccluderIndexBuffer = GraphBuilder.ConvertToExternalBuffer(OccIndices);
		}
		else
		{
			OccPositions = GraphBuilder.RegisterExternalBuffer(OccluderPositionBuffer);
			OccIndices = GraphBuilder.RegisterExternalBuffer(OccluderIndexBuffer);
		}

		FSplatPakOccRasterParameters* RasterParameters = GraphBuilder.AllocParameters<FSplatPakOccRasterParameters>();
		RasterParameters->OccluderPositions = GraphBuilder.CreateSRV(OccPositions);
		RasterParameters->OccluderIndices = GraphBuilder.CreateSRV(OccIndices);
		RasterParameters->RWOccDepth = GraphBuilder.CreateUAV(OccDepth);
		RasterParameters->ModelToClip = Parameters->ModelToClip;
		RasterParameters->OccWidth = OccWidth;
		RasterParameters->OccHeight = OccHeight;
		RasterParameters->OccTriCount = OccTriCount;
		RasterParameters->OccNearW = View.NearClippingDistance;
		TShaderMapRef<FSplatPakOccRasterCS> RasterShader(GetGlobalShaderMap(View.FeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SplatPak.OccluderRaster (%u triangles)", OccTriCount), RasterShader, RasterParameters,
			FIntVector(FMath::DivideAndRoundUp(OccTriCount, 64u), 1, 1));
	}
	Parameters->OccDepth = GraphBuilder.CreateSRV(OccDepth);
	Parameters->OcclusionCull = bOcclusion ? (uint32)OcclusionMode : 0u;
	Parameters->OccWidth = OccWidth;
	Parameters->OccHeight = OccHeight;
	Parameters->OccNearW = View.NearClippingDistance;
	// The largest disc radius the disc shaders can draw (BuildDisc in SplatPakDisc.usf), model units: auto size caps
	// the size-class scale at 2.5; otherwise a splat record's disc spans its two largest scales, a point record's
	// its largest size class (4.5 x the base voxel radius)
	{
		const float MaxExtent = FMath::Max3(Data.QuantExtents.X, Data.QuantExtents.Y, Data.QuantExtents.Z);
		const float ClassRadius = (Data.Format == SplatCore::ERecordFormat::Splat20)
			? 2.0f * FMath::Exp2(Data.ScaleLog2Max) * Settings.SplatScale
			: Settings.SplatScale * FMath::Max(0.0005f, MaxExtent / 1024.0f * 1.35f) * 4.5f;
		const float Classes = (Data.Format == SplatCore::ERecordFormat::Splat20) ? 1.0f : 2.5f;
		Parameters->OccMeshletPad = (MaxLevelRadius > 0.0f) ? Settings.SplatScale * MaxLevelRadius * Classes : ClassRadius;
	}
	Parameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
	if (bGBuffer)
	{
		// The base pass's GBufferA-C (slots 1-3 in every non-Substrate layout); D, E and velocity are left alone
		for (int32 Slot = 1; Slot <= 3; Slot++)
		{
			Parameters->RenderTargets[Slot] = FRenderTargetBinding(GBufferTargets->Output[Slot].GetTexture(), ERenderTargetLoadAction::ELoad);
		}
	}
	// Depth writes even when the base pass itself only tests depth (after a full depth prepass)
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop);
	if (Settings.bShadowMeshFill && !bGBuffer)
	{
		AddMeshFillPasses(GraphBuilder, View, Parameters->ModelToClip, SceneColor, SceneDepth);
	}
	// Streaming with Gaussians on: sorted, blended Gaussians in place of the discs (unlit only)
	if (bStreaming && Settings.bGaussians && !bGBuffer)
	{
		AddGaussianPasses(GraphBuilder, View, Records, Meshlets, *Parameters, SceneColor, SceneDepth);
		return;
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	const TShaderRef<FSplatPakDiscShaderBase> PixelShader = bGBuffer
		? TShaderRef<FSplatPakDiscShaderBase>(TShaderMapRef<FSplatPakDiscGBufferPS>(ShaderMap))
		: TShaderRef<FSplatPakDiscShaderBase>(TShaderMapRef<FSplatPakDiscPS>(ShaderMap));
	const FIntRect ViewRect = View.ViewRect;

	// Shader map refs for the mesh stages exist only where they were compiled, so build them inside the branch.
	if (bMeshShaders)
	{
#if PLATFORM_SUPPORTS_MESH_SHADERS
		TShaderMapRef<FSplatPakDiscAS> AmplificationShader(ShaderMap);
		TShaderMapRef<FSplatPakDiscMS> MeshShader(ShaderMap);
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

	TShaderMapRef<FSplatPakDiscVS> VertexShader(ShaderMap);
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

// SplatPak's gap fill, before the discs: the occluder mesh (depth and colour), so the splats on the far side stay hidden,
// then the shadow mesh in the splats' colours over it without writing depth, so it covers the gaps between sparse splats
// and never hides a splat in front of it
void FSplatPakDrawable::AddMeshFillPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMatrix44f& ModelToClip, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth) const
{
	const SplatCore::FDecodedLevel& Data = *Level;
	auto Upload = [&GraphBuilder](TRefCountPtr<FRDGPooledBuffer>& Pooled, uint32 Stride, uint32 Count, const void* Bytes, const TCHAR* Name) -> FRDGBufferRef
	{
		if (Pooled.IsValid()) return GraphBuilder.RegisterExternalBuffer(Pooled);
		FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(Stride, FMath::Max(Count, 1u)), Name);
		GraphBuilder.QueueBufferUpload(Buffer, Bytes, (uint64)Stride * Count, ERDGInitialDataFlags::NoCopy);
		Pooled = GraphBuilder.ConvertToExternalBuffer(Buffer);
		return Buffer;
	};
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FSplatPakMeshVS> VertexShader(ShaderMap);
	TShaderMapRef<FSplatPakMeshPS> PixelShader(ShaderMap);
	const FIntRect ViewRect = View.ViewRect;
	auto Draw = [&](FRDGBufferRef Positions, FRDGBufferRef Colours, FRDGBufferRef Indices, uint32 IndexCount, bool bWriteDepth, const TCHAR* Name)
	{
		FSplatPakMeshParameters* P = GraphBuilder.AllocParameters<FSplatPakMeshParameters>();
		P->MeshPositions = GraphBuilder.CreateSRV(Positions);
		P->MeshColours = GraphBuilder.CreateSRV(Colours);
		P->MeshIndices = GraphBuilder.CreateSRV(Indices);
		P->ModelToClip = ModelToClip;
		P->LightDirModel = FVector3f(0.5f, 0.8f, 0.6f).GetSafeNormal(); // SplatPak's fixed key light
		P->PreExposure = View.PreExposure;
		P->EmissiveIntensity = Settings.EmissiveIntensity;
		P->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
		P->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad,
			bWriteDepth ? FExclusiveDepthStencil::DepthWrite_StencilNop : FExclusiveDepthStencil::DepthRead_StencilNop);
		GraphBuilder.AddPass(RDG_EVENT_NAME("SplatPak.%s (%u triangles)", Name, IndexCount / 3), P, ERDGPassFlags::Raster,
			[P, VertexShader, PixelShader, ViewRect, IndexCount, bWriteDepth](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
				FGraphicsPipelineStateInitializer PSOInit;
				RHICmdList.ApplyCachedRenderTargets(PSOInit);
				PSOInit.BlendState = TStaticBlendState<>::GetRHI();
				PSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				PSOInit.DepthStencilState = bWriteDepth ? TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI() : TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
				PSOInit.PrimitiveType = PT_TriangleList;
				PSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
				PSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				PSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, PSOInit, 0);
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *P);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *P);
				RHICmdList.DrawPrimitive(0, IndexCount / 3, 1);
			});
	};

	const uint32 OccIndexCount = (uint32)Data.OccluderIndices.size();
	if (OccIndexCount >= 3 && Data.OccluderColours.size() == Data.OccluderPositions.size())
	{
		FRDGBufferRef Positions = Upload(OccluderPositionBuffer, sizeof(float), (uint32)Data.OccluderPositions.size() * 3, Data.OccluderPositions.data(), TEXT("SplatPak.OccluderPositions"));
		FRDGBufferRef Colours = Upload(OccluderColourBuffer, sizeof(uint32), (uint32)Data.OccluderColours.size(), Data.OccluderColours.data(), TEXT("SplatPak.OccluderColours"));
		FRDGBufferRef Indices = Upload(OccluderIndexBuffer, sizeof(uint32), OccIndexCount, Data.OccluderIndices.data(), TEXT("SplatPak.OccluderIndices"));
		Draw(Positions, Colours, Indices, OccIndexCount - OccIndexCount % 3, true, TEXT("Occluder"));
	}
	const uint32 ShadowIndexCount = (uint32)Data.ShadowIndices.size();
	if (ShadowIndexCount >= 3 && Data.ShadowColours.size() == Data.ShadowPositions.size())
	{
		FRDGBufferRef Positions = Upload(ShadowPositionBuffer, sizeof(float), (uint32)Data.ShadowPositions.size() * 3, Data.ShadowPositions.data(), TEXT("SplatPak.ShadowPositions"));
		FRDGBufferRef Colours = Upload(ShadowColourBuffer, sizeof(uint32), (uint32)Data.ShadowColours.size(), Data.ShadowColours.data(), TEXT("SplatPak.ShadowColours"));
		FRDGBufferRef Indices = Upload(ShadowIndexBuffer, sizeof(uint32), ShadowIndexCount, Data.ShadowIndices.data(), TEXT("SplatPak.ShadowIndices"));
		Draw(Positions, Colours, Indices, ShadowIndexCount - ShadowIndexCount % 3, false, TEXT("ShadowMeshFill"));
	}
}

FRDGTextureRef FSplatPakDrawable::AddMeshNormalPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMatrix44f& ModelToClip,
	const FMatrix44f& ModelToWorldNormal, FRDGTextureRef SceneDepth) const
{
	const SplatCore::FDecodedLevel& Data = *Level;
	const uint32 IndexCount = (uint32)(Data.ShadowIndices.size() - Data.ShadowIndices.size() % 3);
	const uint32 VertexCount = (uint32)Data.ShadowPositions.size();
	if (IndexCount < 3 || VertexCount == 0)
	{
		return nullptr;
	}
	// Smooth vertex normals, once: each triangle's area-weighted normal summed into its corners
	if (ShadowNormals.size() != (size_t)VertexCount * 3)
	{
		std::vector<FVector3f> Sum(VertexCount, FVector3f::ZeroVector);
		for (uint32 i = 0; i < IndexCount; i += 3)
		{
			const uint32 A = Data.ShadowIndices[i], B = Data.ShadowIndices[i + 1], C = Data.ShadowIndices[i + 2];
			if (A >= VertexCount || B >= VertexCount || C >= VertexCount) continue;
			const FVector3f PA = ToVector3f(Data.ShadowPositions[A]), PB = ToVector3f(Data.ShadowPositions[B]), PC = ToVector3f(Data.ShadowPositions[C]);
			const FVector3f N = FVector3f::CrossProduct(PB - PA, PC - PA); // Length is twice the area
			Sum[A] += N; Sum[B] += N; Sum[C] += N;
		}
		ShadowNormals.resize((size_t)VertexCount * 3);
		for (uint32 v = 0; v < VertexCount; v++)
		{
			const FVector3f N = Sum[v].GetSafeNormal();
			ShadowNormals[v * 3] = N.X; ShadowNormals[v * 3 + 1] = N.Y; ShadowNormals[v * 3 + 2] = N.Z;
		}
	}
	auto Upload = [&GraphBuilder](TRefCountPtr<FRDGPooledBuffer>& Pooled, uint32 Stride, uint32 Count, const void* Bytes, const TCHAR* Name) -> FRDGBufferRef
	{
		if (Pooled.IsValid()) return GraphBuilder.RegisterExternalBuffer(Pooled);
		FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(Stride, FMath::Max(Count, 1u)), Name);
		GraphBuilder.QueueBufferUpload(Buffer, Bytes, (uint64)Stride * Count, ERDGInitialDataFlags::NoCopy);
		Pooled = GraphBuilder.ConvertToExternalBuffer(Buffer);
		return Buffer;
	};

	const FIntPoint Extent = SceneDepth->Desc.Extent;
	FRDGTextureRef Normals = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Extent, PF_FloatRGBA, FClearValueBinding::Transparent,
		TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("SplatPak.MeshNormals"));
	FRDGTextureRef Depth = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Extent, PF_DepthStencil, FClearValueBinding::DepthFar,
		TexCreate_DepthStencilTargetable | TexCreate_ShaderResource), TEXT("SplatPak.MeshNormalsDepth")); // Readable too: D3D12 asserts when the graph transitions a deny-SRV target

	FSplatPakMeshNormalParameters* P = GraphBuilder.AllocParameters<FSplatPakMeshNormalParameters>();
	P->MeshPositions = GraphBuilder.CreateSRV(Upload(ShadowPositionBuffer, sizeof(float), VertexCount * 3, Data.ShadowPositions.data(), TEXT("SplatPak.ShadowPositions")));
	P->MeshNormals = GraphBuilder.CreateSRV(Upload(ShadowNormalBuffer, sizeof(float), VertexCount * 3, ShadowNormals.data(), TEXT("SplatPak.ShadowNormals")));
	P->MeshIndices = GraphBuilder.CreateSRV(Upload(ShadowIndexBuffer, sizeof(uint32), (uint32)Data.ShadowIndices.size(), Data.ShadowIndices.data(), TEXT("SplatPak.ShadowIndices")));
	P->ModelToClip = ModelToClip;
	P->ModelToWorldNormal = ModelToWorldNormal;
	P->RenderTargets[0] = FRenderTargetBinding(Normals, ERenderTargetLoadAction::EClear);
	P->RenderTargets.DepthStencil = FDepthStencilBinding(Depth, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FSplatPakMeshNormalVS> VertexShader(ShaderMap);
	TShaderMapRef<FSplatPakMeshNormalPS> PixelShader(ShaderMap);
	const FIntRect ViewRect = View.ViewRect;
	GraphBuilder.AddPass(RDG_EVENT_NAME("SplatPak.MeshNormals (%u triangles)", IndexCount / 3), P, ERDGPassFlags::Raster,
		[P, VertexShader, PixelShader, ViewRect, IndexCount](FRHICommandList& RHICmdList)
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
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *P);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *P);
			RHICmdList.DrawPrimitive(0, IndexCount / 3, 1);
		});
	return Normals;
}

void FSplatPakDrawable::SetStreamFrame_RenderThread(TArray<SplatCore::FMeshlet>&& Meshlets)
{
	StreamMeshlets = MoveTemp(Meshlets);
	// Each meshlet's first Gaussian slot, for the sort
	StreamSlotBase.SetNumUninitialized(StreamMeshlets.Num());
	uint32 Slots = 0;
	for (int32 i = 0; i < StreamMeshlets.Num(); i++)
	{
		StreamSlotBase[i] = Slots;
		Slots += StreamMeshlets[i].SplatCount;
	}
	StreamSlotCount = Slots;
}

// Sorted Gaussians (SplatPakGaussian.usf): every splat of the frame's render list gets a slot keyed by its depth along the
// view (far first); a 32-bit radix sort (four 8-bit passes: histogram, scan, scatter) orders them; then each sorted slot is
// drawn as a projected Gaussian, alpha-blended over the scene and depth-tested against it (the occluder mesh included)
void FSplatPakDrawable::AddGaussianPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGBufferRef Records, FRDGBufferRef Meshlets, const FSplatPakDiscParameters& Disc,
	FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth) const
{
	const uint32 MeshletCount = (uint32)StreamMeshlets.Num();
	const uint32 SlotCount = StreamSlotCount;
	if (MeshletCount == 0 || SlotCount == 0) return;
	const uint32 NumBlocks = FMath::DivideAndRoundUp(SlotCount, 1024u);
	const uint32 Padded = NumBlocks * 1024u;

	FRDGBufferRef SlotBase = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MeshletCount), TEXT("SplatPak.GaussianSlotBase"));
	GraphBuilder.QueueBufferUpload(SlotBase, StreamSlotBase.GetData(), MeshletCount * sizeof(uint32));
	FRDGBufferRef SlotInfo = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32) * 2, SlotCount), TEXT("SplatPak.GaussianSlotInfo"));
	FRDGBufferRef Pairs[2] = {
		GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32) * 2, Padded), TEXT("SplatPak.GaussianPairsA")),
		GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32) * 2, Padded), TEXT("SplatPak.GaussianPairsB")) };
	FRDGBufferRef Hist = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 256u * NumBlocks), TEXT("SplatPak.GaussianHistogram"));

	// The view in model space
	const FMatrix ModelToWorld = GetModelToWorld();
	const FMatrix WorldToModel = ModelToWorld.Inverse();
	const FVector3f EyeModel = FVector3f(FVector(WorldToModel.TransformPosition(View.ViewMatrices.GetViewOrigin())));
	const FVector3f ForwardModel = FVector3f(FVector(WorldToModel.TransformVector(View.GetViewDirection())).GetSafeNormal());
	const FMatrix ModelToView = ModelToWorld * View.ViewMatrices.GetViewMatrix();

	auto Fill = [&](auto* P)
	{
		P->ModelToClip = Disc.ModelToClip;
		P->EyeModel = EyeModel;
		P->MeshletCount = MeshletCount;
		P->ForwardModel = ForwardModel;
		P->SlotCount = SlotCount;
		P->QuantMin = Disc.QuantMin;
		P->PaddedSlotCount = Padded;
		P->QuantExtents = Disc.QuantExtents;
		P->NumBlocks = NumBlocks;
		P->RadixPass = 0;
		P->GroupsX = FMath::Min(MeshletCount, 65535u);
		P->ModelToView = FMatrix44f(ModelToView);
		P->Viewport = FVector2f((float)View.ViewRect.Width(), (float)View.ViewRect.Height());
		P->Proj00 = (float)View.ViewMatrices.GetProjectionMatrix().M[0][0];
		P->ScaleLog2Min = Disc.ScaleLog2Min;
		P->ScaleLog2Max = Disc.ScaleLog2Max;
		P->PreExposure = Disc.PreExposure;
		P->EmissiveIntensity = Disc.EmissiveIntensity;
		P->StreamFlags = Disc.StreamFlags;
		P->GlowIntensity = Disc.GlowIntensity;
		P->GlowHue = Disc.GlowHue;
		for (int32 i = 0; i < 2; i++) P->LevelRadii[i] = Disc.LevelRadii[i];
	};
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	auto SortParameters = [&](uint32 Pass, FRDGBufferRef In, FRDGBufferRef Out)
	{
		FSplatPakGaussianSortParameters* P = GraphBuilder.AllocParameters<FSplatPakGaussianSortParameters>();
		Fill(P);
		P->RadixPass = Pass;
		P->Records = GraphBuilder.CreateSRV(Records);
		P->Meshlets = GraphBuilder.CreateSRV(Meshlets);
		P->MeshletSlotBase = GraphBuilder.CreateSRV(SlotBase);
		P->SlotInfo = GraphBuilder.CreateUAV(SlotInfo);
		P->PairsIn = GraphBuilder.CreateUAV(In);
		P->PairsOut = GraphBuilder.CreateUAV(Out);
		P->Hist = GraphBuilder.CreateUAV(Hist);
		return P;
	};

	// Expand: one group per meshlet (folded over y past 65535)
	{
		TShaderMapRef<FSplatPakGaussianExpandCS> Shader(ShaderMap);
		const uint32 GroupsX = FMath::Min(MeshletCount, 65535u);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SplatPak.GaussianExpand (%u splats)", SlotCount), Shader, SortParameters(0, Pairs[0], Pairs[1]),
			FIntVector(GroupsX, FMath::DivideAndRoundUp(MeshletCount, GroupsX), 1));
	}
	// Four 8-bit passes, ping-ponging the pairs; the sorted order ends in Pairs[0]
	TShaderMapRef<FSplatPakGaussianHistogramCS> Histogram(ShaderMap);
	TShaderMapRef<FSplatPakGaussianScanCS> Scan(ShaderMap);
	TShaderMapRef<FSplatPakGaussianScatterCS> Scatter(ShaderMap);
	for (uint32 Pass = 0; Pass < 4; Pass++)
	{
		FRDGBufferRef In = Pairs[Pass & 1], Out = Pairs[(Pass + 1) & 1];
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SplatPak.GaussianHistogram %u", Pass), Histogram, SortParameters(Pass, In, Out), FIntVector(NumBlocks, 1, 1));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SplatPak.GaussianScan %u", Pass), Scan, SortParameters(Pass, In, Out), FIntVector(1, 1, 1));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SplatPak.GaussianScatter %u", Pass), Scatter, SortParameters(Pass, In, Out), FIntVector(NumBlocks, 1, 1));
	}

	// Draw: six vertices per sorted slot, premultiplied alpha over the scene, depth-tested without writing
	FSplatPakGaussianDrawParameters* P = GraphBuilder.AllocParameters<FSplatPakGaussianDrawParameters>();
	Fill(P);
	P->Records = GraphBuilder.CreateSRV(Records);
	P->Meshlets = GraphBuilder.CreateSRV(Meshlets);
	P->SortedPairs = GraphBuilder.CreateSRV(Pairs[0]);
	P->SortedSlotInfo = GraphBuilder.CreateSRV(SlotInfo);
	P->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
	P->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilNop);
	TShaderMapRef<FSplatPakGaussianVS> VertexShader(ShaderMap);
	TShaderMapRef<FSplatPakGaussianPS> PixelShader(ShaderMap);
	const FIntRect ViewRect = View.ViewRect;
	GraphBuilder.AddPass(RDG_EVENT_NAME("SplatPak.Gaussians (%u splats)", SlotCount), P, ERDGPassFlags::Raster,
		[P, VertexShader, PixelShader, ViewRect, SlotCount](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
			FGraphicsPipelineStateInitializer PSOInit;
			RHICmdList.ApplyCachedRenderTargets(PSOInit);
			PSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
			PSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			PSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
			PSOInit.PrimitiveType = PT_TriangleList;
			PSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			PSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			PSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			SetGraphicsPipelineState(RHICmdList, PSOInit, 0);
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *P);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *P);
			RHICmdList.DrawPrimitive(0, SlotCount * 2, 1);
		});
}
