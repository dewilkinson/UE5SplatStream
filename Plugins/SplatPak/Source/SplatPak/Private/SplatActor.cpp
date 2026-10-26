// SplatActor.cpp

#include "SplatActor.h"

#include "SplatComponent.h"

ASplatActor::ASplatActor()
{
	SplatComponent = CreateDefaultSubobject<USplatComponent>(TEXT("SplatComponent"));
	RootComponent = SplatComponent;
}
