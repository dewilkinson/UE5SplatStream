// SurfelComponent.cpp

#include "SurfelComponent.h"

#include "SurfelAsset.h"
#include "SurfelSceneProxy.h"
#include "RenderingThread.h"

DEFINE_LOG_CATEGORY_STATIC(LogSurfels, Log, All);

USurfelComponent::USurfelComponent()
{
	CastShadow = false; // Splat shadow casting is a later, separate depth pass per light
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	bUseAsOccluder = false;
}

void USurfelComponent::SetSurfelAsset(USurfelAsset* NewAsset)
{
	if (SurfelAsset != NewAsset)
	{
		SurfelAsset = NewAsset;
		UpdateBounds();
		MarkRenderStateDirty();
	}
}

FMatrix USurfelComponent::GetModelToLocal() const
{
	const double S = UnitsToCentimeters;
	if (bConvertYUpToZUp)
	{
		// Rows are the images of the model axes (Unreal's row-vector convention): X -> X, Y -> Z, Z -> Y.
		return FMatrix(FPlane(S, 0, 0, 0), FPlane(0, 0, S, 0), FPlane(0, S, 0, 0), FPlane(0, 0, 0, 1));
	}
	return FScaleMatrix(FVector(S));
}

void USurfelComponent::SetEmissiveIntensity(float NewIntensity)
{
	EmissiveIntensity = FMath::Max(NewIntensity, 0.0f);
	if (FSurfelSceneProxy* Proxy = static_cast<FSurfelSceneProxy*>(SceneProxy))
	{
		// Only a shader constant changes: hand it to the render thread instead of rebuilding the proxy.
		ENQUEUE_RENDER_COMMAND(SetSurfelEmissiveIntensity)([Proxy, Intensity = EmissiveIntensity](FRHICommandListImmediate&)
		{
			Proxy->SetEmissiveIntensity_RenderThread(Intensity);
		});
	}
}

FPrimitiveSceneProxy* USurfelComponent::CreateSceneProxy()
{
	if (!SurfelAsset)
	{
		return nullptr;
	}

	FString Error;
	FSurfelLevelPtr Level = SurfelAsset->GetDecodedLevel(ResidentLevel, &Error);
	if (!Level.IsValid())
	{
		UE_LOG(LogSurfels, Warning, TEXT("%s: could not decode %s: %s"), *GetPathName(), *SurfelAsset->GetName(), *Error);
		return nullptr;
	}

	FSurfelDrawSettings Settings;
	Settings.SplatScale = SplatScale;
	Settings.Coverage = Coverage;
	Settings.Orient = (Orientation == ESurfelOrientation::Billboard) ? ESurfelOrient::Billboard : ESurfelOrient::Disc;
	Settings.EmissiveIntensity = EmissiveIntensity;
	return new FSurfelSceneProxy(this, MoveTemp(Level), Settings);
}

FBoxSphereBounds USurfelComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!SurfelAsset)
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0);
	}
	return FBoxSphereBounds(SurfelAsset->GetModelBounds().TransformBy(GetModelToLocal() * LocalToWorld.ToMatrixWithScale()));
}

#if WITH_EDITOR
void USurfelComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(USurfelComponent, EmissiveIntensity))
	{
		SetEmissiveIntensity(EmissiveIntensity);
		return;
	}
	UpdateBounds();
	MarkRenderStateDirty();
}
#endif
