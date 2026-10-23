// SurfelActor.h
// A placeable actor holding one USurfelComponent, for dropping a scan into a level.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SurfelActor.generated.h"

class USurfelComponent;

UCLASS(ClassGroup = Rendering, ComponentWrapperClass)
class SURFELSRUNTIME_API ASurfelActor : public AActor
{
	GENERATED_BODY()

public:
	ASurfelActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Surfels")
	TObjectPtr<USurfelComponent> SurfelComponent;
};
