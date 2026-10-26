// SplatComponent.h
// Places a USplatAsset in a level. Step 1 keeps one level of the package resident and draws it
// as opaque discs, depth-tested against the rest of the scene.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "SplatComponent.generated.h"

class USplatAsset;
class UStaticMeshComponent;

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

	// Multiplies every disc radius (g_Radius in SplatPak).
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

	// The package's shadow mesh (USplatAsset::ShadowMesh), placed hidden: it casts the model's shadow from every light
	// type, virtual shadow maps included. Packages without one (format 11 or older) cast no shadow.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Shadow Mesh")
	bool bShadowMeshCastShadow = true;

	// Lets the hidden shadow mesh contribute to Lumen and distance-field lighting (its mesh distance field).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Shadow Mesh")
	bool bShadowMeshAffectLumen = true;

	// Gives the model collision from the shadow mesh (line traces and physics); off, traces pass through.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Shadow Mesh")
	bool bShadowMeshCollision = true;

	// The hidden component holding the shadow mesh (transient: rebuilt whenever this component registers).
	UFUNCTION(BlueprintPure, Category = "SplatPak|Shadow Mesh")
	UStaticMeshComponent* GetShadowMeshComponent() const { return ShadowMeshComponent; }

	// Package units to Unreal centimetres (packages are in metres).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Import Frame", meta = (ClampMin = "0.0001"))
	float UnitsToCentimeters = 100.0f;

	// Packages are right-handed, Y-up; Unreal is left-handed, Z-up. Swapping Y and Z converts both.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Import Frame")
	bool bConvertYUpToZUp = true;

	// Package model space to this component's local space.
	FMatrix GetModelToLocal() const;

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	//~ UPrimitiveComponent
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void UpdateShadowMeshComponent();

	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
	TObjectPtr<UStaticMeshComponent> ShadowMeshComponent;
};
