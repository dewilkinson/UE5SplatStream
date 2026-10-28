// SplatPakPedestal.cpp

#include "SplatPakPedestal.h"

#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SplatPakAsset.h"
#include "SplatPakComponent.h"

ASplatPakPedestal::ASplatPakPedestal()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	PlinthComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plinth"));
	PlinthComponent->SetupAttachment(RootComponent);
	SculptureComponent = CreateDefaultSubobject<USplatPakComponent>(TEXT("Sculpture"));
	SculptureComponent->SetupAttachment(RootComponent);
	SculptureComponent->bStreaming = true;
	SculptureComponent->bUnthrottled = true;
	KeyLightComponent = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	KeyLightComponent->SetupAttachment(RootComponent);
	KeyLightComponent->SetIntensityUnits(ELightUnits::Candelas);
	KeyLightComponent->SetLightColor(FLinearColor::FromSRGBColor(FColor(255, 236, 210)));
	KeyLightComponent->SetAttenuationRadius(450.0f);
	UplightComponent = CreateDefaultSubobject<USpotLightComponent>(TEXT("Uplight"));
	UplightComponent->SetupAttachment(RootComponent);
	UplightComponent->SetIntensityUnits(ELightUnits::Candelas);
	UplightComponent->SetLightColor(FLinearColor::FromSRGBColor(FColor(255, 230, 200)));
	UplightComponent->SetInnerConeAngle(14.0f);
	UplightComponent->SetOuterConeAngle(26.0f);
	UplightComponent->SetAttenuationRadius(1500.0f);
	UplightComponent->SetCastShadows(true);
}

void ASplatPakPedestal::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateLayout();
}

void ASplatPakPedestal::UpdateLayout()
{
	// The plinth, and its top in the actor's frame
	PlinthComponent->SetStaticMesh(PlinthMesh);
	float PlinthTop = 0.0f;
	if (PlinthMesh)
	{
		PlinthTop = (float)PlinthMesh->GetBoundingBox().Max.Z;
		if (UMaterialInterface* Material = PlinthComponent->GetMaterial(0))
		{
			// Its own instance (reused once made), so each pedestal's glow is its own
			if (UMaterialInstanceDynamic* Instance = PlinthComponent->CreateDynamicMaterialInstance(0, Material))
			{
				Instance->SetScalarParameterValue(TEXT("Emissive"), PlinthEmissive);
			}
		}
	}

	// The sculpture: scaled to SculptureHeight, its bounds' base on the plinth's top, centred over the plinth
	if (SculptureComponent->SplatPakAsset != Sculpture)
	{
		SculptureComponent->SetSplatPakAsset(Sculpture);
	}
	SculptureComponent->SetRelativeRotation(FRotator(0.0f, FrontYawOffset, 0.0f));
	SculptureComponent->SetRelativeScale3D(FVector::OneVector);
	if (Sculpture)
	{
		const FBoxSphereBounds Local = SculptureComponent->CalcBounds(FTransform(FRotator(0.0f, FrontYawOffset, 0.0f)));
		const float Height = 2.0f * (float)Local.BoxExtent.Z;
		if (Height > KINDA_SMALL_NUMBER)
		{
			const float Scale = SculptureHeight / Height;
			SculptureComponent->SetRelativeScale3D(FVector(Scale));
			const FVector Centre = Local.Origin * Scale;
			const float Base = (float)(Local.Origin.Z - Local.BoxExtent.Z) * Scale;
			SculptureComponent->SetRelativeLocation(FVector(-Centre.X, -Centre.Y, PlinthTop - Base));
		}
	}

	// The lights, in front (+X) of the sculpture
	KeyLightComponent->SetVisibility(bKeyLight);
	KeyLightComponent->SetIntensity(KeyLightCandelas);
	KeyLightComponent->SetRelativeLocation(FVector(150.0f, 0.0f, 260.0f));
	UplightComponent->SetVisibility(bUplight);
	UplightComponent->SetIntensity(UplightCandelas);
	const FVector UplightAt(80.0f, 0.0f, 5.0f);
	const FVector Aim(0.0f, 0.0f, PlinthTop + 0.5f * SculptureHeight);
	UplightComponent->SetRelativeLocation(UplightAt);
	UplightComponent->SetRelativeRotation((Aim - UplightAt).Rotation());
}
