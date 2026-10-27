// SplatPakActor.cpp

#include "SplatPakActor.h"

#include "SplatPakComponent.h"

ASplatPakActor::ASplatPakActor()
{
	SplatPakComponent = CreateDefaultSubobject<USplatPakComponent>(TEXT("SplatComponent")); // The subobject keeps its name: placed actors store their component under it
	RootComponent = SplatPakComponent;
}
