// SplatPakViewExtension.h
// Hooks the splat passes into Unreal's frame. Unlit drawables draw opaque discs in the pre-post-process
// callback: after lighting, before TSR, depth-tested and depth-writing against the scene. Lit ones
// (FSplatPakDrawable::DrawsLit) draw into the GBuffer right after the base pass, so the lighting passes shade them.

#pragma once

#include "SceneViewExtension.h"

class FSplatPakViewExtension : public FSceneViewExtensionBase
{
public:
	FSplatPakViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& View, const FRenderTargetBindingSlots& RenderTargets,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) override;
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) override;
};
