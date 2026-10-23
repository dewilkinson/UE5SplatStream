// SurfelActor.cpp

#include "SurfelActor.h"

#include "SurfelComponent.h"

ASurfelActor::ASurfelActor()
{
	SurfelComponent = CreateDefaultSubobject<USurfelComponent>(TEXT("SurfelComponent"));
	RootComponent = SurfelComponent;
}
