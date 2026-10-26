// SplatSceneProxy.h
// Render-thread representative of a USplatComponent: an FPrimitiveSceneProxy for Unreal's scene
// (bounds, visibility, transform) and an FSplatDrawable for the splat passes.

#pragma once

#include "PrimitiveSceneProxy.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SplatDrawable.h"

class USplatComponent;

class FSplatSceneProxy final : public FPrimitiveSceneProxy, public FSplatDrawable
{
public:
	FSplatSceneProxy(const USplatComponent* Component, FSplatLevelPtr InLevel, const FSplatDrawSettings& InSettings);

	//~ FPrimitiveSceneProxy
	virtual SIZE_T GetTypeHash() const override;
	virtual uint32 GetMemoryFootprint() const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
#else
	virtual void CreateRenderThreadResources() override;
#endif
	virtual void DestroyRenderThreadResources() override;

	//~ FSplatDrawable
	virtual bool IsVisibleInView(const FSceneView& View) const override;
	virtual FMatrix GetModelToWorld() const override;

private:
	FMatrix ModelToLocal;
};
