// SurfelSceneProxy.cpp

#include "SurfelSceneProxy.h"

#include "SceneInterface.h"
#include "SceneView.h"
#include "SurfelComponent.h"

FSurfelSceneProxy::FSurfelSceneProxy(const USurfelComponent* Component, FSurfelLevelPtr InLevel, const FSurfelDrawSettings& InSettings)
	: FPrimitiveSceneProxy(Component)
	, FSurfelDrawable(MoveTemp(InLevel), InSettings)
	, ModelToLocal(Component->GetModelToLocal())
{
}

SIZE_T FSurfelSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

uint32 FSurfelSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize() + GetGPUMemoryBytes();
}

FPrimitiveViewRelevance FSurfelSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	// The discs are drawn by FSurfelViewExtension, not through mesh batches; relevance only
	// tells the scene the primitive is visible so bounds, selection outlines and stats work.
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	return Result;
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
void FSurfelSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
#else
void FSurfelSceneProxy::CreateRenderThreadResources()
#endif
{
	RegisterDrawable();
}

void FSurfelSceneProxy::DestroyRenderThreadResources()
{
	UnregisterDrawable();
}

bool FSurfelSceneProxy::IsVisibleInView(const FSceneView& View) const
{
	if (!View.Family || &GetScene() != View.Family->Scene || !IsShown(&View))
	{
		return false;
	}
	const FBoxSphereBounds& Bounds = GetBounds();
	return View.ViewFrustum.IntersectBox(Bounds.Origin, Bounds.BoxExtent);
}

FMatrix FSurfelSceneProxy::GetModelToWorld() const
{
	return ModelToLocal * GetLocalToWorld();
}
