// SplatPakSceneProxy.h
// Render-thread representative of a USplatPakComponent: an FPrimitiveSceneProxy for Unreal's scene
// (bounds, visibility, transform) and an FSplatPakDrawable for the splat passes.

#pragma once

#include "PrimitiveSceneProxy.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SplatPakDrawable.h"

class USplatPakComponent;

class FSplatPakSceneProxy final : public FPrimitiveSceneProxy, public FSplatPakDrawable
{
public:
	FSplatPakSceneProxy(const USplatPakComponent* Component, FSplatPakLevelPtr InLevel, const FSplatPakDrawSettings& InSettings, FSplatPakStreamSourcePtr InStream = nullptr);

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

	//~ FSplatPakDrawable
	virtual bool IsVisibleInView(const FSceneView& View) const override;
	virtual FMatrix GetModelToWorld() const override;

private:
	FMatrix ModelToLocal;
};
