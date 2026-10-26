// SplatActor.h
// A placeable actor holding one USplatComponent, for dropping a scan into a level.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SplatActor.generated.h"

class USplatComponent;

UCLASS(ClassGroup = Rendering, ComponentWrapperClass)
class SPLATPAK_API ASplatActor : public AActor
{
	GENERATED_BODY()

public:
	ASplatActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak")
	TObjectPtr<USplatComponent> SplatComponent;
};
