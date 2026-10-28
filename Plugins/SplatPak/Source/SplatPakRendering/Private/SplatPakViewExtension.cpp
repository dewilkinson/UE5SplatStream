// SplatPakViewExtension.cpp

#include "SplatPakViewExtension.h"

#include "SplatPakDrawable.h"
#include "RenderGraphBuilder.h"
#include "SceneRendering.h"
#include "SceneRenderTargetParameters.h"

#if __has_include("PostProcess/PostProcessInputs.h")
#include "PostProcess/PostProcessInputs.h"
#else
#include "PostProcess/PostProcessing.h"
#endif

void FSplatPakViewExtension::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& View, const FRenderTargetBindingSlots& RenderTargets,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	const TArray<FSplatPakDrawable*>& Drawables = FSplatPakDrawable::GetRegisteredDrawables();
	if (Drawables.IsEmpty() || !View.bIsViewInfo)
	{
		return;
	}

	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
	RDG_EVENT_SCOPE(GraphBuilder, "SplatPak (lit)");
	for (const FSplatPakDrawable* Drawable : Drawables)
	{
		if (Drawable->DrawsLit(View) && Drawable->IsVisibleInView(View))
		{
			Drawable->AddDiscGBufferPass(GraphBuilder, ViewInfo, RenderTargets);
		}
	}
}

void FSplatPakViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	const TArray<FSplatPakDrawable*>& Drawables = FSplatPakDrawable::GetRegisteredDrawables();
	if (Drawables.IsEmpty() || !View.bIsViewInfo)
	{
		return;
	}

	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
	const FSceneTextureUniformParameters* SceneTextures = Inputs.SceneTextures->GetParameters().GetContents();
	FRDGTextureRef SceneColor = SceneTextures->SceneColorTexture;
	FRDGTextureRef SceneDepth = SceneTextures->SceneDepthTexture;
	if (!SceneColor || !SceneDepth)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "SplatPak");
	for (const FSplatPakDrawable* Drawable : Drawables)
	{
		if (!Drawable->DrawsLit(View) && Drawable->IsVisibleInView(View))
		{
			Drawable->AddDiscPass(GraphBuilder, ViewInfo, SceneColor, SceneDepth);
		}
	}
}
