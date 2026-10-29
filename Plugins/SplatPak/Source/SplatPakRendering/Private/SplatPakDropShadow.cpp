// SplatPakDropShadow.cpp

#include "SplatPakDropShadow.h"

#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "ShaderParameterStruct.h"
#include "SplatPakDrawable.h"
#include "SplatPakPackageCore.h"
#include "SplatPakVeil.h"

static TAutoConsoleVariable<int32> CVarSplatPakDropShadow(
	TEXT("r.SplatPak.DropShadow"),
	1,
	TEXT("1 = draw the drop shadows of splat objects that ask for one (USplatPakComponent::bDropShadow; default). 0 = never."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarSplatPakDropShadowBlur(
	TEXT("r.SplatPak.DropShadow.Blur"),
	0.004f,
	TEXT("The drop shadows' softness, a Gaussian sigma as a share of the view's height."),
	ECVF_RenderThreadSafe);

BEGIN_SHADER_PARAMETER_STRUCT(FSplatPakDropShadowMaskParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, MeshPositions)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MeshIndices)
	SHADER_PARAMETER(FMatrix44f, ModelToPlane)
	SHADER_PARAMETER(FMatrix44f, PlaneToClip)
	SHADER_PARAMETER(FVector2f, ClipMin)
	SHADER_PARAMETER(FVector2f, ClipMax)
	SHADER_PARAMETER(float, Opacity)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FSplatPakDropShadowMaskVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplatPakDropShadowMaskVS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakDropShadowMaskVS, FGlobalShader);
	using FParameters = FSplatPakDropShadowMaskParameters;
};

class FSplatPakDropShadowMaskPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplatPakDropShadowMaskPS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakDropShadowMaskPS, FGlobalShader);
	using FParameters = FSplatPakDropShadowMaskParameters;
};

class FSplatPakDropShadowCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplatPakDropShadowCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakDropShadowCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Mask)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSplatPakDropShadowMaskVS, "/Plugin/SplatPak/Private/SplatPakDropShadow.usf", "MaskVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FSplatPakDropShadowMaskPS, "/Plugin/SplatPak/Private/SplatPakDropShadow.usf", "MaskPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FSplatPakDropShadowCompositePS, "/Plugin/SplatPak/Private/SplatPakDropShadow.usf", "CompositePS", SF_Pixel);

FMatrix SplatPakDropShadow::FloorProjection(const FVector& Direction, double FloorZ)
{
	// p' = p - D (p.z - h) / D.z, lifted a centimetre so it sits on the floor rather than in it
	const double Kx = -Direction.X / Direction.Z, Ky = -Direction.Y / Direction.Z;
	return FMatrix(
		FPlane(1.0, 0.0, 0.0, 0.0),
		FPlane(0.0, 1.0, 0.0, 0.0),
		FPlane(Kx, Ky, 0.0, 0.0),
		FPlane(-Kx * FloorZ, -Ky * FloorZ, FloorZ + 1.0, 1.0));
}

void FSplatPakDrawable::AddDropShadowMask(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef Mask, FRDGTextureRef SceneDepth) const
{
	const FVector Direction(Settings.DropShadowDirection);
	if (Direction.Z > -0.05) return;
	const FMatrix Projection = SplatPakDropShadow::FloorProjection(Direction, Settings.DropShadowFloorZ);
	// The shadow is flattened in world space, then taken to translated world (precise far from the origin) for the clip
	// rectangle and the projection
	const FVector PreView = View.ViewMatrices.GetPreViewTranslation();
	const FMatrix ToTranslated = FTranslationMatrix(PreView);
	const FMatrix44f PlaneToClip = FMatrix44f(View.ViewMatrices.GetTranslatedWorldToClip());
	const FVector2f Unbounded(1.0e9f, 1.0e9f);
	TShaderMapRef<FSplatPakDropShadowMaskVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FSplatPakDropShadowMaskPS> PixelShader(View.ShaderMap);
	const FIntRect ViewRect = View.ViewRect;

	auto Upload = [&GraphBuilder](TRefCountPtr<FRDGPooledBuffer>& Pooled, uint32 Stride, uint32 Count, const void* Bytes, const TCHAR* Name) -> FRDGBufferRef
	{
		if (Pooled.IsValid()) return GraphBuilder.RegisterExternalBuffer(Pooled);
		FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(Stride, FMath::Max(Count, 1u)), Name);
		GraphBuilder.QueueBufferUpload(Buffer, Bytes, (uint64)Stride * Count, ERDGInitialDataFlags::None);
		Pooled = GraphBuilder.ConvertToExternalBuffer(Buffer);
		return Buffer;
	};
	auto Draw = [&](FRDGBufferRef Positions, FRDGBufferRef Indices, uint32 IndexCount, const FMatrix& LocalToPlane, const FVector2f& ClipMin,
		const FVector2f& ClipMax, float Opacity, const TCHAR* Name)
	{
		FSplatPakDropShadowMaskParameters* P = GraphBuilder.AllocParameters<FSplatPakDropShadowMaskParameters>();
		P->MeshPositions = GraphBuilder.CreateSRV(Positions);
		P->MeshIndices = GraphBuilder.CreateSRV(Indices);
		P->ModelToPlane = FMatrix44f(LocalToPlane * ToTranslated);
		P->PlaneToClip = PlaneToClip;
		P->ClipMin = ClipMin;
		P->ClipMax = ClipMax;
		P->Opacity = Opacity;
		P->RenderTargets[0] = FRenderTargetBinding(Mask, ERenderTargetLoadAction::ELoad);
		P->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilNop);
		GraphBuilder.AddPass(RDG_EVENT_NAME("SplatPak.DropShadow.%s (%u triangles)", Name, IndexCount / 3), P, ERDGPassFlags::Raster,
			[P, VertexShader, PixelShader, ViewRect, IndexCount](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
				FGraphicsPipelineStateInitializer PSOInit;
				RHICmdList.ApplyCachedRenderTargets(PSOInit);
				PSOInit.BlendState = TStaticBlendState<CW_RED, BO_Max, BF_One, BF_One>::GetRHI();
				PSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				PSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
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

	// The shadow mesh, in model space
	const SplatCore::FDecodedLevel& Data = *Level;
	const uint32 ShadowIndexCount = (uint32)(Data.ShadowIndices.size() - Data.ShadowIndices.size() % 3);
	if (ShadowIndexCount >= 3 && !Data.ShadowPositions.empty())
	{
		FRDGBufferRef Positions = Upload(ShadowPositionBuffer, sizeof(float), (uint32)Data.ShadowPositions.size() * 3, Data.ShadowPositions.data(), TEXT("SplatPak.ShadowPositions"));
		FRDGBufferRef Indices = Upload(ShadowIndexBuffer, sizeof(uint32), (uint32)Data.ShadowIndices.size(), Data.ShadowIndices.data(), TEXT("SplatPak.ShadowIndices"));
		Draw(Positions, Indices, ShadowIndexCount, GetModelToWorld() * Projection, -Unbounded, Unbounded, Settings.DropShadowOpacity, TEXT("ShadowMesh"));

		// On the plinth's top as well, kept within it (inset a centimetre): the sun's shadow, and a contact shadow straight
		// down under the base, so the sculpture stands on the stone rather than floating over it
		if (Settings.bDropShadowBox)
		{
			const double TopZ = Settings.DropShadowBoxMax.Z + 0.5;
			const FVector2f TopMin = FVector2f(Settings.DropShadowBoxMin.X + 1.0f, Settings.DropShadowBoxMin.Y + 1.0f) + FVector2f((float)PreView.X, (float)PreView.Y);
			const FVector2f TopMax = FVector2f(Settings.DropShadowBoxMax.X - 1.0f, Settings.DropShadowBoxMax.Y - 1.0f) + FVector2f((float)PreView.X, (float)PreView.Y);
			Draw(Positions, Indices, ShadowIndexCount, GetModelToWorld() * SplatPakDropShadow::FloorProjection(Direction, TopZ), TopMin, TopMax,
				Settings.DropShadowOpacity, TEXT("ShadowMeshOnTop"));
			Draw(Positions, Indices, ShadowIndexCount, GetModelToWorld() * SplatPakDropShadow::FloorProjection(FVector(0.0, 0.0, -1.0), TopZ), TopMin, TopMax,
				0.8f * Settings.DropShadowOpacity, TEXT("ContactOnTop"));
		}
	}

	// The box (the plinth), in world space
	if (Settings.bDropShadowBox)
	{
		if (!DropShadowBoxPositions.IsValid())
		{
			const FVector3f& A = Settings.DropShadowBoxMin;
			const FVector3f& B = Settings.DropShadowBoxMax;
			static const uint32 BoxIndices[36] = { 0,1,3, 0,3,2, 4,6,7, 4,7,5, 0,4,5, 0,5,1, 2,3,7, 2,7,6, 0,2,6, 0,6,4, 1,5,7, 1,7,3 };
			float Corners[24];
			for (int32 i = 0; i < 8; i++)
			{
				Corners[i * 3 + 0] = (i & 4) ? B.X : A.X;
				Corners[i * 3 + 1] = (i & 2) ? B.Y : A.Y;
				Corners[i * 3 + 2] = (i & 1) ? B.Z : A.Z;
			}
			Upload(DropShadowBoxPositions, sizeof(float), 24, Corners, TEXT("SplatPak.DropShadowBoxPositions"));
			Upload(DropShadowBoxIndices, sizeof(uint32), 36, BoxIndices, TEXT("SplatPak.DropShadowBoxIndices"));
		}
		Draw(GraphBuilder.RegisterExternalBuffer(DropShadowBoxPositions), GraphBuilder.RegisterExternalBuffer(DropShadowBoxIndices), 36,
			Projection, -Unbounded, Unbounded, Settings.DropShadowOpacity, TEXT("Box"));
	}
}

void SplatPakDropShadow::AddPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, TConstArrayView<const FSplatPakDrawable*> Drawables, FRDGTextureRef SceneColor,
	FRDGTextureRef SceneDepth)
{
	if (CVarSplatPakDropShadow.GetValueOnRenderThread() == 0 || !SceneColor || !SceneDepth || SceneColor->Desc.NumSamples > 1) return;
	bool bAny = false;
	for (const FSplatPakDrawable* Drawable : Drawables) { bAny |= Drawable->HasDropShadow(); }
	if (!bAny) return;

	RDG_EVENT_SCOPE(GraphBuilder, "SplatPak drop shadows");
	const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(SceneColor->Desc.Extent, PF_R8, FClearValueBinding::Black,
		TexCreate_RenderTargetable | TexCreate_ShaderResource);
	FRDGTextureRef Mask = GraphBuilder.CreateTexture(Desc, TEXT("SplatPak.DropShadowMask"));
	AddClearRenderTargetPass(GraphBuilder, Mask, FLinearColor::Black);
	for (const FSplatPakDrawable* Drawable : Drawables)
	{
		if (Drawable->HasDropShadow()) { Drawable->AddDropShadowMask(GraphBuilder, View, Mask, SceneDepth); }
	}

	// Soft edges, then the view's colour times one minus the mask
	SplatPakVeil::AddBlur(GraphBuilder, View, Mask, CVarSplatPakDropShadowBlur.GetValueOnRenderThread() * (float)View.ViewRect.Height());
	TShaderMapRef<FSplatPakDropShadowCompositePS> PixelShader(View.ShaderMap);
	FSplatPakDropShadowCompositePS::FParameters* P = GraphBuilder.AllocParameters<FSplatPakDropShadowCompositePS::FParameters>();
	P->Mask = Mask;
	P->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("Composite"), PixelShader, P, View.ViewRect,
		TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_Zero>::GetRHI());
}
