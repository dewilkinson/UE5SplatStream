// SurfelComponent.h
// Places a USurfelAsset in a level. Step 1 keeps one level of the package resident and draws it
// as opaque discs, depth-tested against the rest of the scene.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "SurfelComponent.generated.h"

class USurfelAsset;

UENUM(BlueprintType)
enum class ESurfelOrientation : uint8
{
	Disc      UMETA(ToolTip = "Disc in each surfel's tangent plane"),
	Billboard UMETA(ToolTip = "Camera-facing quad"),
};

UCLASS(ClassGroup = Rendering, meta = (BlueprintSpawnableComponent), hidecategories = (Collision, Physics, Navigation, Materials))
class SURFELSRUNTIME_API USurfelComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	USurfelComponent();

	UFUNCTION(BlueprintCallable, Category = "Surfels")
	void SetSurfelAsset(USurfelAsset* NewAsset);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surfels")
	TObjectPtr<USurfelAsset> SurfelAsset;

	// LOD level kept resident: 0 is the finest, -1 the coarsest the package holds.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surfels", meta = (ClampMin = "-1"))
	int32 ResidentLevel = -1;

	// Multiplies every disc radius (g_Radius in surfels).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surfels", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float SplatScale = 1.0f;

	// Disc radius as a multiple of the level's median point spacing (Auto Splat Size coverage).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surfels", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float Coverage = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surfels")
	ESurfelOrientation Orientation = ESurfelOrientation::Disc;

	// Brightness of the scan's stored colours. They're baked light, drawn like an emissive surface,
	// so Unreal's exposure affects them: raise this until the scan looks right in your lighting.
	// Changing it is cheap (no rebuild, no re-upload) and it can be animated in Sequencer.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Surfels", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0", SliderExponent = "2.0"))
	float EmissiveIntensity = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "Surfels")
	void SetEmissiveIntensity(float NewIntensity);

	// Package units to Unreal centimetres (surfels packages are in metres).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surfels|Import Frame", meta = (ClampMin = "0.0001"))
	float UnitsToCentimeters = 100.0f;

	// Packages are right-handed, Y-up; Unreal is left-handed, Z-up. Swapping Y and Z converts both.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surfels|Import Frame")
	bool bConvertYUpToZUp = true;

	// Package model space to this component's local space.
	FMatrix GetModelToLocal() const;

	//~ UPrimitiveComponent
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
