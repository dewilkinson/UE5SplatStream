// SplatComponent.h
// Places a USplatAsset in a level. Step 1 keeps one level of the package resident and draws it
// as opaque discs, depth-tested against the rest of the scene.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "SplatComponent.generated.h"

class USplatAsset;

UENUM(BlueprintType)
enum class ESplatOrientation : uint8
{
	Disc      UMETA(ToolTip = "Disc in each splat's tangent plane"),
	Billboard UMETA(ToolTip = "Camera-facing quad"),
};

UCLASS(ClassGroup = Rendering, meta = (BlueprintSpawnableComponent), hidecategories = (Collision, Physics, Navigation, Materials))
class SPLATPAK_API USplatComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	USplatComponent();

	UFUNCTION(BlueprintCallable, Category = "SplatPak")
	void SetSplatAsset(USplatAsset* NewAsset);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak")
	TObjectPtr<USplatAsset> SplatAsset;

	// LOD level kept resident: 0 is the finest, -1 the coarsest the package holds.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak", meta = (ClampMin = "-1"))
	int32 ResidentLevel = -1;

	// Multiplies every disc radius (g_Radius in SplatLab).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float SplatScale = 1.0f;

	// Disc radius as a multiple of the level's median point spacing (Auto Splat Size coverage).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float Coverage = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak")
	ESplatOrientation Orientation = ESplatOrientation::Disc;

	// Brightness of the scan's stored colours. They're baked light, drawn like an emissive surface,
	// so Unreal's exposure affects them: raise this until the scan looks right in your lighting.
	// Changing it is cheap (no rebuild, no re-upload) and it can be animated in Sequencer.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "SplatPak", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0", SliderExponent = "2.0"))
	float EmissiveIntensity = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "SplatPak")
	void SetEmissiveIntensity(float NewIntensity);

	// Package units to Unreal centimetres (packages are in metres).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Import Frame", meta = (ClampMin = "0.0001"))
	float UnitsToCentimeters = 100.0f;

	// Packages are right-handed, Y-up; Unreal is left-handed, Z-up. Swapping Y and Z converts both.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Import Frame")
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
