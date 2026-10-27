// SplatPakSceneProxy.cpp

#include "SplatPakSceneProxy.h"

#include "SceneInterface.h"
#include "SceneView.h"
#include "SplatPakComponent.h"

FSplatPakSceneProxy::FSplatPakSceneProxy(const USplatPakComponent* Component, FSplatPakLevelPtr InLevel, const FSplatPakDrawSettings& InSettings, FSplatPakStreamSourcePtr InStream)
	: FPrimitiveSceneProxy(Component)
	, FSplatPakDrawable(MoveTemp(InLevel), InSettings)
	, ModelToLocal(Component->GetModelToLocal())
{
	Stream = MoveTemp(InStream); // Not yet seen by the render thread: set directly
}

SIZE_T FSplatPakSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

uint32 FSplatPakSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize() + GetGPUMemoryBytes();
}

FPrimitiveViewRelevance FSplatPakSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	// The discs are drawn by FSplatPakViewExtension, not through mesh batches; relevance only
	// tells the scene the primitive is visible so bounds, selection outlines and stats work.
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	return Result;
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
void FSplatPakSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
#else
void FSplatPakSceneProxy::CreateRenderThreadResources()
#endif
{
	RegisterDrawable();
}

void FSplatPakSceneProxy::DestroyRenderThreadResources()
{
	UnregisterDrawable();
}

bool FSplatPakSceneProxy::IsVisibleInView(const FSceneView& View) const
{
	if (!View.Family || &GetScene() != View.Family->Scene || !IsShown(&View))
	{
		return false;
	}
	const FBoxSphereBounds& PrimitiveBounds = GetBounds();
	return View.ViewFrustum.IntersectBox(PrimitiveBounds.Origin, PrimitiveBounds.BoxExtent);
}

FMatrix FSplatPakSceneProxy::GetModelToWorld() const
{
	return ModelToLocal * GetLocalToWorld();
}
