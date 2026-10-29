// SplatPakPedestal.cpp

#include "SplatPakPedestal.h"

#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SplatPakAsset.h"
#include "SplatPakComponent.h"
#include "SplatPakPackageCore.h"

namespace
{
	// A scan's extent in model space without its strays: the package's bounds take every splat, so a few floaters below a
	// sculpture's base stood it that far above its plinth. Per axis, the 1st percentile of the coarsest level's meshlets'
	// low corners to the 99th of their high ones (a meshlet's high corner is its low one reflected through its centre).
	// Read once per asset; invalid when the level cannot be decoded.
	FBox RobustModelBounds(USplatPakAsset* Asset)
	{
		static TMap<TWeakObjectPtr<USplatPakAsset>, FBox> Cache;
		if (const FBox* Found = Cache.Find(Asset)) return *Found;
		FBox Result(ForceInit);
		if (FSplatPakLevelPtr Level = Asset->GetDecodedLevel(-1))
		{
			TArray<float> Lo[3], Hi[3];
			for (const SplatCore::FMeshlet& M : Level->Meshlets)
			{
				const float C[3] = { M.Center.X, M.Center.Y, M.Center.Z }, A[3] = { M.AabbMin.X, M.AabbMin.Y, M.AabbMin.Z };
				for (int32 Axis = 0; Axis < 3; Axis++) { Lo[Axis].Add(A[Axis]); Hi[Axis].Add(2.0f * C[Axis] - A[Axis]); }
			}
			if (Lo[0].Num() >= 8)
			{
				FVector Min, Max;
				for (int32 Axis = 0; Axis < 3; Axis++)
				{
					Lo[Axis].Sort();
					Hi[Axis].Sort();
					const int32 N = Lo[Axis].Num();
					Min[Axis] = Lo[Axis][FMath::Clamp(N / 100, 0, N - 1)];
					Max[Axis] = Hi[Axis][FMath::Clamp(N - 1 - N / 100, 0, N - 1)];
				}
				Result = FBox(Min, Max);
			}
		}
		Cache.Add(Asset, Result);
		return Result;
	}
}

ASplatPakPedestal::ASplatPakPedestal()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	PlinthComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plinth"));
	PlinthComponent->SetupAttachment(RootComponent);
	SculptureComponent = CreateDefaultSubobject<USplatPakComponent>(TEXT("Sculpture"));
	SculptureComponent->SetupAttachment(RootComponent);
	SculptureComponent->bStreaming = true;
	SculptureComponent->bUnthrottled = true;
	// No gap fill: a sculpture is dense enough without it, and a scan's shadow mesh can reach below its splats (a base
	// cropped from the splats but not the mesh), which then drew over the plinth in flat colour
	SculptureComponent->bShadowMeshFill = false;
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
	SculptureComponent->bAlwaysFinest = bSculptureAlwaysFinest;
	SculptureComponent->SetRelativeRotation(FRotator(0.0f, FrontYawOffset, 0.0f));
	SculptureComponent->SetRelativeScale3D(FVector::OneVector);
	if (Sculpture)
	{
		// The scan's own extent without its strays (RobustModelBounds), in the sculpture's frame at scale 1
		const FBox Model = RobustModelBounds(Sculpture);
		const FBoxSphereBounds Local = Model.IsValid
			? FBoxSphereBounds(Model.TransformBy(SculptureComponent->GetModelToLocal() * FRotationMatrix(FRotator(0.0f, FrontYawOffset, 0.0f))))
			: SculptureComponent->CalcBounds(FTransform(FRotator(0.0f, FrontYawOffset, 0.0f)));
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

	// The drop shadow, drawn with the sculpture's splats: its shadow mesh and the plinth's bounds, on this actor's floor
	FBox PlinthBox(ForceInit);
	if (PlinthMesh)
	{
		PlinthBox = PlinthMesh->GetBoundingBox().TransformBy(PlinthComponent->GetComponentTransform());
	}
	SculptureComponent->SetDropShadow(bDropShadow, DropShadowDirection, DropShadowOpacity, (float)GetActorLocation().Z, PlinthBox);

	// The colour match, once the sculpture is where it stands; the plinth takes the room's colour too (its material's
	// "Colour" parameter, when it has one), so it is the stone of this floor rather than of another
	if (ColourMatchScene && ColourMatch > 0.0f && SculptureComponent->MatchColours(ColourMatchScene, ColourMatch, ColourMatchRadius))
	{
		if (UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(PlinthComponent->GetMaterial(0)); Instance && bPlinthMatchesRoom)
		{
			Instance->SetVectorParameterValue(TEXT("Colour"), SculptureComponent->GetMatchedRoomColour());
		}
	}
	else
	{
		SculptureComponent->ClearColourMatch();
	}
}
