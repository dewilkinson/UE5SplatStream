// SplatSceneProxy.cpp

#include "SplatSceneProxy.h"

#include "SceneInterface.h"
#include "SceneView.h"
#include "SplatComponent.h"

FSplatSceneProxy::FSplatSceneProxy(const USplatComponent* Component, FSplatLevelPtr InLevel, const FSplatDrawSettings& InSettings)
	: FPrimitiveSceneProxy(Component)
	, FSplatDrawable(MoveTemp(InLevel), InSettings)
	, ModelToLocal(Component->GetModelToLocal())
{
}

SIZE_T FSplatSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

uint32 FSplatSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize() + GetGPUMemoryBytes();
}

FPrimitiveViewRelevance FSplatSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	// The discs are drawn by FSplatViewExtension, not through mesh batches; relevance only
	// tells the scene the primitive is visible so bounds, selection outlines and stats work.
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	return Result;
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
void FSplatSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
#else
void FSplatSceneProxy::CreateRenderThreadResources()
#endif
{
	RegisterDrawable();
}

void FSplatSceneProxy::DestroyRenderThreadResources()
{
	UnregisterDrawable();
}

bool FSplatSceneProxy::IsVisibleInView(const FSceneView& View) const
{
	if (!View.Family || &GetScene() != View.Family->Scene || !IsShown(&View))
	{
		return false;
	}
	const FBoxSphereBounds& PrimitiveBounds = GetBounds();
	return View.ViewFrustum.IntersectBox(PrimitiveBounds.Origin, PrimitiveBounds.BoxExtent);
}

FMatrix FSplatSceneProxy::GetModelToWorld() const
{
	return ModelToLocal * GetLocalToWorld();
}
