// SplatPakViewExtension.cpp

#include "SplatPakViewExtension.h"

#include "SplatPakDrawable.h"
#include "SplatPakDropShadow.h"
#include "SplatPakVeil.h"
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

	// Environment scans first, then the objects farthest first: each scan's Gaussians are sorted and blended on their own
	// and write no depth, so a scan drawn later blends over whatever is drawn before it
	TArray<const FSplatPakDrawable*, TInlineAllocator<16>> Ordered;
	for (const FSplatPakDrawable* Drawable : Drawables)
	{
		if (!Drawable->DrawsLit(View) && Drawable->IsVisibleInView(View)) { Ordered.Add(Drawable); }
	}
	const FVector Eye = View.ViewMatrices.GetViewOrigin();
	Ordered.StableSort([&Eye](const FSplatPakDrawable& A, const FSplatPakDrawable& B)
	{
		if (A.IsEnvironment() != B.IsEnvironment()) { return A.IsEnvironment(); }
		return FVector::DistSquared(A.GetModelToWorld().GetOrigin(), Eye) > FVector::DistSquared(B.GetModelToWorld().GetOrigin(), Eye);
	});

	RDG_EVENT_SCOPE(GraphBuilder, "SplatPak");
	for (const FSplatPakDrawable* Drawable : Ordered)
	{
		Drawable->AddDiscPass(GraphBuilder, ViewInfo, SceneColor, SceneDepth);
	}

	// The objects' drop shadows on the floor (see SplatPakDropShadow.h), then the veil over everything the view drew,
	// before post-processing (see SplatPakVeil.h)
	SplatPakDropShadow::AddPasses(GraphBuilder, ViewInfo, Ordered, SceneColor, SceneDepth);
	SplatPakVeil::AddPass(GraphBuilder, ViewInfo, SceneColor);
}
