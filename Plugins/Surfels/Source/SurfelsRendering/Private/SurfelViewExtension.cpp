// SurfelViewExtension.cpp

#include "SurfelViewExtension.h"

#include "SurfelDrawable.h"
#include "RenderGraphBuilder.h"
#include "SceneRendering.h"
#include "SceneRenderTargetParameters.h"

#if __has_include("PostProcess/PostProcessInputs.h")
#include "PostProcess/PostProcessInputs.h"
#else
#include "PostProcess/PostProcessing.h"
#endif

void FSurfelViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	const TArray<FSurfelDrawable*>& Drawables = FSurfelDrawable::GetRegisteredDrawables();
	if (Drawables.IsEmpty() || !View.bIsViewInfo)
	{
		return;
	}

	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
	const FSceneTextureUniformParameters* SceneTextures = Inputs.SceneTextures->GetParameters();
	FRDGTextureRef SceneColor = SceneTextures->SceneColorTexture;
	FRDGTextureRef SceneDepth = SceneTextures->SceneDepthTexture;
	if (!SceneColor || !SceneDepth)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "Surfels");
	for (const FSurfelDrawable* Drawable : Drawables)
	{
		if (Drawable->IsVisibleInView(View))
		{
			Drawable->AddDiscPass(GraphBuilder, ViewInfo, SceneColor, SceneDepth);
		}
	}
}
