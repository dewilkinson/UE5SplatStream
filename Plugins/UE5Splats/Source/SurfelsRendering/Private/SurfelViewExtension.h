// SurfelViewExtension.h
// Hooks the surfel passes into Unreal's frame. Step 1 draws opaque discs in the pre-post-process
// callback: after lighting, before TSR, depth-tested and depth-writing against the scene.

#pragma once

#include "SceneViewExtension.h"

class FSurfelViewExtension : public FSceneViewExtensionBase
{
public:
	FSurfelViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) override;
};
