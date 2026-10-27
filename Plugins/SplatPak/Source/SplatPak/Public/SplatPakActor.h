// SplatPakActor.h
// A placeable actor holding one USplatPakComponent, for dropping a scan into a level.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SplatPakActor.generated.h"

class USplatPakComponent;

UCLASS(ClassGroup = Rendering, ComponentWrapperClass)
class SPLATPAK_API ASplatPakActor : public AActor
{
	GENERATED_BODY()

public:
	ASplatPakActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak")
	TObjectPtr<USplatPakComponent> SplatPakComponent;
};
