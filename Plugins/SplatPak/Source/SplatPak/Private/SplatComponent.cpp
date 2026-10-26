// SplatComponent.cpp

#include "SplatComponent.h"

#include "SplatAsset.h"
#include "SplatSceneProxy.h"
#include "RenderingThread.h"

DEFINE_LOG_CATEGORY_STATIC(LogSplatPak, Log, All);

USplatComponent::USplatComponent()
{
	CastShadow = false; // Splat shadow casting is a later, separate depth pass per light
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	bUseAsOccluder = false;
}

void USplatComponent::SetSplatAsset(USplatAsset* NewAsset)
{
	if (SplatAsset != NewAsset)
	{
		SplatAsset = NewAsset;
		UpdateBounds();
		MarkRenderStateDirty();
	}
}

FMatrix USplatComponent::GetModelToLocal() const
{
	const double S = UnitsToCentimeters;
	if (bConvertYUpToZUp)
	{
		// Rows are the images of the model axes (Unreal's row-vector convention): X -> X, Y -> Z, Z -> Y.
		return FMatrix(FPlane(S, 0, 0, 0), FPlane(0, 0, S, 0), FPlane(0, S, 0, 0), FPlane(0, 0, 0, 1));
	}
	return FScaleMatrix(FVector(S));
}

void USplatComponent::SetEmissiveIntensity(float NewIntensity)
{
	EmissiveIntensity = FMath::Max(NewIntensity, 0.0f);
	if (FSplatSceneProxy* Proxy = static_cast<FSplatSceneProxy*>(SceneProxy))
	{
		// Only a shader constant changes: hand it to the render thread instead of rebuilding the proxy.
		ENQUEUE_RENDER_COMMAND(SetSplatEmissiveIntensity)([Proxy, Intensity = EmissiveIntensity](FRHICommandListImmediate&)
		{
			Proxy->SetEmissiveIntensity_RenderThread(Intensity);
		});
	}
}

FPrimitiveSceneProxy* USplatComponent::CreateSceneProxy()
{
	if (!SplatAsset)
	{
		return nullptr;
	}

	FString Error;
	FSplatLevelPtr Level = SplatAsset->GetDecodedLevel(ResidentLevel, &Error);
	if (!Level.IsValid())
	{
		UE_LOG(LogSplatPak, Warning, TEXT("%s: could not decode %s: %s"), *GetPathName(), *SplatAsset->GetName(), *Error);
		return nullptr;
	}

	FSplatDrawSettings Settings;
	Settings.SplatScale = SplatScale;
	Settings.Coverage = Coverage;
	Settings.Orient = (Orientation == ESplatOrientation::Billboard) ? ESplatOrient::Billboard : ESplatOrient::Disc;
	Settings.EmissiveIntensity = EmissiveIntensity;
	return new FSplatSceneProxy(this, MoveTemp(Level), Settings);
}

FBoxSphereBounds USplatComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!SplatAsset)
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0);
	}
	return FBoxSphereBounds(SplatAsset->GetModelBounds().TransformBy(GetModelToLocal() * LocalToWorld.ToMatrixWithScale()));
}

#if WITH_EDITOR
void USplatComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(USplatComponent, EmissiveIntensity))
	{
		SetEmissiveIntensity(EmissiveIntensity);
		return;
	}
	UpdateBounds();
	MarkRenderStateDirty();
}
#endif
