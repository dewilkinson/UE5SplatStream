// SurfelSceneProxy.h
// Render-thread representative of a USurfelComponent: an FPrimitiveSceneProxy for Unreal's scene
// (bounds, visibility, transform) and an FSurfelDrawable for the surfel passes.

#pragma once

#include "PrimitiveSceneProxy.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SurfelDrawable.h"

class USurfelComponent;

class FSurfelSceneProxy final : public FPrimitiveSceneProxy, public FSurfelDrawable
{
public:
	FSurfelSceneProxy(const USurfelComponent* Component, FSurfelLevelPtr InLevel, const FSurfelDrawSettings& InSettings);

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

	//~ FSurfelDrawable
	virtual bool IsVisibleInView(const FSceneView& View) const override;
	virtual FMatrix GetModelToWorld() const override;

private:
	FMatrix ModelToLocal;
};
