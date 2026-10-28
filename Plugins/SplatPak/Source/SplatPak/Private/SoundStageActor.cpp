// SoundStageActor.cpp

#include "SoundStageActor.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/CollisionProfile.h"
#include "SplatPakComponent.h"
#include "SplatPakPedestal.h"

namespace
{
	FColor KindColour(ESoundStageVolumeKind Kind)
	{
		switch (Kind)
		{
		case ESoundStageVolumeKind::Floor:    return FColor(60, 200, 90);
		case ESoundStageVolumeKind::Stairs:   return FColor(255, 150, 40);
		case ESoundStageVolumeKind::Railing:  return FColor(250, 220, 40);
		case ESoundStageVolumeKind::Boundary: return FColor(230, 50, 50);
		case ESoundStageVolumeKind::Trigger:  return FColor(40, 210, 230);
		default:                              return FColor(230, 230, 230);
		}
	}

	bool ShapeContains(const UShapeComponent* Shape, const FVector& World)
	{
		const FVector L = Shape->GetComponentTransform().InverseTransformPositionNoScale(World);
		if (const UBoxComponent* Box = Cast<UBoxComponent>(Shape))
		{
			const FVector E = Box->GetUnscaledBoxExtent();
			return FMath::Abs(L.X) <= E.X && FMath::Abs(L.Y) <= E.Y && FMath::Abs(L.Z) <= E.Z;
		}
		if (const USphereComponent* Sphere = Cast<USphereComponent>(Shape))
		{
			return L.Size() <= Sphere->GetUnscaledSphereRadius();
		}
		if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Shape))
		{
			const float R = Capsule->GetUnscaledCapsuleRadius();
			const float Segment = Capsule->GetUnscaledCapsuleHalfHeight() - R;
			return FVector(L.X, L.Y, L.Z - FMath::Clamp(L.Z, -Segment, Segment)).Size() <= R;
		}
		return false;
	}
}

ASoundStageActor::ASoundStageActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BackdropComponent = CreateDefaultSubobject<USplatPakComponent>(TEXT("Backdrop"));
	BackdropComponent->SetupAttachment(RootComponent);
	BackdropComponent->bStreaming = true;
	BackdropComponent->bUnthrottled = true;
	PedestalClass = ASplatPakPedestal::StaticClass();
}

void ASoundStageActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void ASoundStageActor::PostLoad()
{
	Super::PostLoad();
	WatchStage();
}

void ASoundStageActor::BeginDestroy()
{
	if (USoundStageAsset* Old = WatchedStage.Get())
	{
		Old->OnStageChanged.Remove(StageChangedHandle);
	}
	Super::BeginDestroy();
}

#if WITH_EDITOR
void ASoundStageActor::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event); // Reruns the construction script, which rebuilds
	WatchStage();
}
#endif

void ASoundStageActor::WatchStage()
{
	if (WatchedStage.Get() == Stage) { return; }
	if (USoundStageAsset* Old = WatchedStage.Get())
	{
		Old->OnStageChanged.Remove(StageChangedHandle);
	}
	WatchedStage = Stage;
	if (Stage)
	{
		StageChangedHandle = Stage->OnStageChanged.AddUObject(this, &ASoundStageActor::OnStageChanged);
	}
}

void ASoundStageActor::OnStageChanged(USoundStageAsset* Changed)
{
#if WITH_EDITOR
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		RerunConstructionScripts();
		return;
	}
#endif
	Rebuild();
}

void ASoundStageActor::Clear()
{
	for (auto& [Name, Shape] : VolumeComponents) { if (Shape) { Shape->DestroyComponent(); } }
	for (UShapeComponent* Face : BoundaryFaces) { if (Face) { Face->DestroyComponent(); } }
	for (auto& [Name, Piece] : PieceComponents) { if (Piece) { Piece->DestroyComponent(); } }
	VolumeComponents.Reset();
	BoundaryFaces.Reset();
	PieceComponents.Reset();
}

void ASoundStageActor::Rebuild()
{
	WatchStage();
	Clear();
	if (BackdropComponent->SplatPakAsset != (Stage ? Stage->Backdrop.Get() : nullptr))
	{
		BackdropComponent->SetSplatPakAsset(Stage ? Stage->Backdrop.Get() : nullptr);
	}
	if (!Stage) { return; }
	for (const FSoundStageVolume& Volume : Stage->Volumes) { AddVolume(Volume); }
	for (const FSoundStagePiece& Piece : Stage->Pieces) { AddPiece(Piece); }
}

UShapeComponent* ASoundStageActor::MakeShape(const FSoundStageVolume& Volume, FName ComponentName)
{
	UShapeComponent* Shape = nullptr;
	const FVector HalfCm = Volume.Size * 50.0; // Metres to half extents in centimetres
	switch (Volume.Shape)
	{
	case ESoundStageShape::Sphere:
	{
		USphereComponent* Sphere = NewObject<USphereComponent>(this, ComponentName);
		Sphere->SetSphereRadius((float)HalfCm.X, false);
		Shape = Sphere;
		break;
	}
	case ESoundStageShape::Capsule:
	{
		UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(this, ComponentName);
		Capsule->SetCapsuleSize((float)HalfCm.X, (float)FMath::Max(HalfCm.X, HalfCm.Y), false);
		Shape = Capsule;
		break;
	}
	default:
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(this, ComponentName);
		Box->SetBoxExtent(FVector(HalfCm.X, HalfCm.Z, HalfCm.Y), false); // The volume's Y is up, Z across
		Box->SetLineThickness(2.0f);
		Shape = Box;
		break;
	}
	}
	Shape->CreationMethod = EComponentCreationMethod::UserConstructionScript;
	Shape->SetupAttachment(RootComponent);
	Shape->SetRelativeTransform(USoundStageAsset::GetVolumeTransform(Volume));
	Shape->ShapeColor = KindColour(Volume.Kind);
	Shape->SetVisibility(bShowVolumes);
	Shape->SetHiddenInGame(!(bShowVolumes && bShowVolumesInGame));
	Shape->SetCanEverAffectNavigation(Volume.Kind != ESoundStageVolumeKind::Trigger && Volume.Kind != ESoundStageVolumeKind::Boundary);
	return Shape;
}

void ASoundStageActor::AddVolume(const FSoundStageVolume& Volume)
{
	UShapeComponent* Shape = MakeShape(Volume, MakeUniqueObjectName(this, UShapeComponent::StaticClass(), FName(TEXT("Volume_") + Volume.Name.ToString())));
	const bool bBoundary = Volume.Kind == ESoundStageVolumeKind::Boundary;
	if (!bVolumeCollision || bBoundary)
	{
		Shape->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	}
	else if (Volume.Kind == ESoundStageVolumeKind::Trigger)
	{
		Shape->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
		Shape->SetGenerateOverlapEvents(true);
	}
	else
	{
		Shape->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	}
	Shape->RegisterComponent();
	VolumeComponents.Add(Volume.Name, Shape);

	// A boundary blocks from within: a thin slab outside each face of its box (spheres and capsules only test)
	if (bBoundary && bVolumeCollision && Volume.Shape == ESoundStageShape::Box)
	{
		const FVector E = FVector(Volume.Size.X, Volume.Size.Z, Volume.Size.Y) * 50.0;
		const double T = 10.0; // Half the slab's thickness, cm
		const FTransform Frame = USoundStageAsset::GetVolumeTransform(Volume);
		for (int Axis = 0; Axis < 3; Axis++)
		{
			for (int Sign = -1; Sign <= 1; Sign += 2)
			{
				FVector Offset = FVector::ZeroVector, Extent = E + FVector(T);
				Offset[Axis] = Sign * (E[Axis] + T);
				Extent[Axis] = T;
				UBoxComponent* Face = NewObject<UBoxComponent>(this, MakeUniqueObjectName(this, UBoxComponent::StaticClass(), TEXT("BoundaryFace")));
				Face->CreationMethod = EComponentCreationMethod::UserConstructionScript;
				Face->SetupAttachment(RootComponent);
				Face->SetRelativeTransform(FTransform(Frame.GetRotation(), Frame.TransformPosition(Offset)));
				Face->SetBoxExtent(Extent, false);
				Face->SetVisibility(false);
				Face->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
				Face->SetCanEverAffectNavigation(false);
				Face->RegisterComponent();
				BoundaryFaces.Add(Face);
			}
		}
	}
}

void ASoundStageActor::AddPiece(const FSoundStagePiece& Piece)
{
	UChildActorComponent* Child = NewObject<UChildActorComponent>(this, MakeUniqueObjectName(this, UChildActorComponent::StaticClass(), FName(TEXT("Piece_") + Piece.Name.ToString())));
	Child->CreationMethod = EComponentCreationMethod::UserConstructionScript;
	Child->SetupAttachment(RootComponent);
	Child->SetRelativeTransform(FTransform(FRotator(0.0f, Piece.Facing, 0.0f), USoundStageAsset::PackageToStage(Piece.Floor)));
	Child->SetChildActorClass(PedestalClass ? PedestalClass.Get() : ASplatPakPedestal::StaticClass());
	Child->RegisterComponent();
	if (ASplatPakPedestal* Pedestal = Cast<ASplatPakPedestal>(Child->GetChildActor()))
	{
		Pedestal->Sculpture = Piece.Sculpture;
		Pedestal->SculptureHeight = Piece.Height * 100.0f;
		Pedestal->FrontYawOffset = Piece.FrontYawOffset;
		Pedestal->PlinthMesh = Piece.Plinth;
		Pedestal->PlinthEmissive = Piece.PlinthEmissive;
		Pedestal->bUplight = Piece.bUplight;
		Pedestal->UpdateLayout();
	}
	PieceComponents.Add(Piece.Name, Child);
}

// ---- Accessors ---------------------------------------------------------------------------------------------------

UShapeComponent* ASoundStageActor::GetVolumeComponent(FName Name) const
{
	const TObjectPtr<UShapeComponent>* Shape = VolumeComponents.Find(Name);
	return Shape ? Shape->Get() : nullptr;
}

ASplatPakPedestal* ASoundStageActor::GetPiece(FName Name) const
{
	const TObjectPtr<UChildActorComponent>* Child = PieceComponents.Find(Name);
	return Child && *Child ? Cast<ASplatPakPedestal>((*Child)->GetChildActor()) : nullptr;
}

TArray<FName> ASoundStageActor::GetPieceNames() const
{
	TArray<FName> Names;
	PieceComponents.GetKeys(Names);
	return Names;
}

TArray<FName> ASoundStageActor::GetVolumesAt(FVector WorldLocation) const
{
	TArray<FName> Names;
	for (const auto& [Name, Shape] : VolumeComponents)
	{
		if (Shape && ShapeContains(Shape, WorldLocation)) { Names.Add(Name); }
	}
	return Names;
}

bool ASoundStageActor::IsInsideBoundary(FVector WorldLocation) const
{
	if (!Stage) { return true; }
	for (const FSoundStageVolume& Volume : Stage->Volumes)
	{
		const UShapeComponent* Shape = Volume.Kind == ESoundStageVolumeKind::Boundary ? GetVolumeComponent(Volume.Name) : nullptr;
		if (Shape && !ShapeContains(Shape, WorldLocation)) { return false; }
	}
	return true;
}

FTransform ASoundStageActor::GetStartTransform() const
{
	if (!Stage) { return GetActorTransform(); }
	const FVector Look = GetActorTransform().TransformVectorNoScale(USoundStageAsset::PackageToStage(Stage->StartLook)).GetSafeNormal();
	return FTransform(Look.Rotation(), PackageToWorld(Stage->StartEye));
}

FVector ASoundStageActor::PackageToWorld(FVector PackagePoint) const
{
	return GetActorTransform().TransformPosition(USoundStageAsset::PackageToStage(PackagePoint));
}

FVector ASoundStageActor::WorldToPackage(FVector WorldLocation) const
{
	return USoundStageAsset::StageToPackage(GetActorTransform().InverseTransformPosition(WorldLocation));
}

void ASoundStageActor::SetVolumesVisible(bool bVisible)
{
	bShowVolumes = bVisible;
	for (auto& [Name, Shape] : VolumeComponents)
	{
		if (Shape)
		{
			Shape->SetVisibility(bVisible);
			Shape->SetHiddenInGame(!(bVisible && bShowVolumesInGame));
		}
	}
}
