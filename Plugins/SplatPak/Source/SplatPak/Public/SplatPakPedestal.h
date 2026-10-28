// SplatPakPedestal.h
// A sculpture on a plinth, as one actor to place in a splat scene: the plinth (any static mesh; Nanite works), the splat
// sculpture standing on its top at a chosen height, a key light in front and above, and optionally an uplight on the
// floor in front, aimed at the sculpture so its shadow climbs the wall behind it. The actor's forward (+X) is the way the
// sculpture faces; FrontYawOffset corrects a scan whose own front is not +X. Everything is laid out by UpdateLayout (run
// by the construction script), so editing a property in the Details panel updates the actor at once. A SoundStage
// (ASoundStageActor) places one per piece.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SplatPakPedestal.generated.h"

class UPointLightComponent;
class USplatPakAsset;
class USplatPakComponent;
class USpotLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class SPLATPAK_API ASplatPakPedestal : public AActor
{
	GENERATED_BODY()

public:
	ASplatPakPedestal();

	// The sculpture: its package, and its height standing on the plinth (cm)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestal")
	TObjectPtr<USplatPakAsset> Sculpture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestal", meta = (ClampMin = "1.0", UIMax = "300.0"))
	float SculptureHeight = 100.0f;

	// Degrees added to the sculpture's yaw, for a scan whose front is not the actor's forward
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestal")
	float FrontYawOffset = 0.0f;

	// The plinth: any static mesh with its origin at the centre of its base; the sculpture stands on its bounds' top.
	// None: the sculpture stands on the floor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestal")
	TObjectPtr<UStaticMesh> PlinthMesh;

	// The plinth's own glow, as a share of its colour (its material's "Emissive" scalar parameter, when it has one): the
	// splats around it are baked light, so a little emissive keeps the plinth from reading dark beside them; the lights
	// then add its shading and shadows
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestal", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float PlinthEmissive = 0.3f;

	// A warm light in front of the sculpture and above it (from straight above, the sculpture would shadow its plinth)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestal|Lighting")
	bool bKeyLight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestal|Lighting", meta = (ClampMin = "0.0", EditCondition = "bKeyLight"))
	float KeyLightCandelas = 60.0f;

	// A spot on the floor in front, aimed up at the sculpture and its plinth, casting their shadow up the wall behind
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestal|Lighting")
	bool bUplight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pedestal|Lighting", meta = (ClampMin = "0.0", EditCondition = "bUplight"))
	float UplightCandelas = 300.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pedestal")
	TObjectPtr<UStaticMeshComponent> PlinthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pedestal")
	TObjectPtr<USplatPakComponent> SculptureComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pedestal|Lighting")
	TObjectPtr<UPointLightComponent> KeyLightComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pedestal|Lighting")
	TObjectPtr<USpotLightComponent> UplightComponent;

	// Lays the plinth, sculpture and lights out from the properties above. Call it after changing them from code.
	UFUNCTION(BlueprintCallable, Category = "Pedestal")
	void UpdateLayout();

	virtual void OnConstruction(const FTransform& Transform) override;
};
