// SoundStageActor.cpp

#include "SoundStageActor.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/CollisionProfile.h"
#include "SplatPakComponent.h"
#include "SplatPakPedestal.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogSoundStage, Log, All);

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
	// The stage's volumes are the scene's collision; the scan's own shadow mesh is too coarse to walk on (on the palace
	// stairs it caught a walking visitor against the balustrade), so it only casts shadows and feeds Lumen here
	BackdropComponent->bShadowMeshCollision = false;
	// No gap fill either: the streamed Gaussians cover the scan's gaps, and the fill's occluder (depth and flat colour)
	// stood in front of the pieces' plinths on the palace landing, so they drew as flat brown blocks
	BackdropComponent->bShadowMeshFill = false;
	// But its occluder still writes depth (only), so a piece behind the scan's walls is hidden: the vases were drawn over
	// the stairwell's walls once the backdrop drew first
	BackdropComponent->bOccluderDepth = false; // Off: with it the palace's own Gaussians all failed against it (black view); to fix
	// The level's scan stays resident: what has streamed in is kept when the visitor turns away or walks on, rather than
	// released and streamed again (and veiled again) on the way back
	BackdropComponent->bReleaseUnusedSegments = false;
	// And all of it comes in: the view first, then the rest of the level in the background (Greedy), so the upper
	// landings are loaded by the time the visitor gets there
	BackdropComponent->StreamingPolicy = ESplatPakStreamingPolicy::Greedy;
	BackdropComponent->bGreedyEnvironment = true;
	PedestalClass = ASplatPakPedestal::StaticClass();
	PrimaryActorTick.bCanEverTick = true; // The stream sequence in play (Tick)
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

// A play world (play in editor, a cooked level) holds the stage's components, but not the transient maps of them, and
// each pedestal is spawned again from its class: without its sculpture, plinth or lights. Both are put back here.
void ASoundStageActor::BeginPlay()
{
	ReconnectComponents();
	// A cooked level does not hold them at all (the construction script's components are not saved, and a packaged game
	// does not run it): build the stage here from its asset, the volumes, boundary and pieces
	if (Stage && (VolumeComponents.Num() < Stage->Volumes.Num() || PieceComponents.Num() < Stage->Pieces.Num()))
	{
		UE_LOG(LogSoundStage, Log, TEXT("%s: building the stage at play (%d of %d volumes, %d of %d pieces in the level)"), *GetName(),
			VolumeComponents.Num(), Stage->Volumes.Num(), PieceComponents.Num(), Stage->Pieces.Num());
		Rebuild();
	}
	if (Stage)
	{
		for (const FSoundStagePiece& Piece : Stage->Pieces)
		{
			if (TObjectPtr<UChildActorComponent>* Child = PieceComponents.Find(Piece.Name)) { ApplyPiece(*Child, Piece); }
		}
	}

	// The stream sequence: the backdrop streams its view alone first (Conservative), the pieces held
	StreamStep = INDEX_NONE;
	if (Stage && GetWorld() && GetWorld()->IsGameWorld())
	{
		StreamOrders.Reset();
		for (const FSoundStagePiece& Piece : Stage->Pieces)
		{
			StreamOrders.AddUnique(Piece.StreamOrder);
			if (ASplatPakPedestal* Pedestal = GetPiece(Piece.Name)) { Pedestal->SculptureComponent->SetStreamHold(true); }
		}
		StreamOrders.Sort();
		BackdropComponent->bGreedyEnvironment = false;
		StreamStep = 0;
		StreamStepSeconds = 0.0f;
	}
	Super::BeginPlay();
}

// Pieces with HideBelowEye: hidden while the visitor's eye (the player camera, package frame) is below that height
void ASoundStageActor::UpdateHiddenPieces()
{
	const UWorld* World = GetWorld();
	if (!Stage || !World || !World->IsGameWorld()) return;
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!Camera) return;
	const double EyeHeight = WorldToPackage(Camera->GetCameraLocation()).Y;
	for (const FSoundStagePiece& Piece : Stage->Pieces)
	{
		if (Piece.HideBelowEye <= -1.0e5f) continue;
		if (ASplatPakPedestal* Pedestal = GetPiece(Piece.Name))
		{
			const bool bHide = EyeHeight < Piece.HideBelowEye;
			if (Pedestal->IsHidden() != bHide) Pedestal->SetActorHiddenInGame(bHide);
		}
	}
}

void ASoundStageActor::ReleaseStreamOrder(int32 Order)
{
	for (const FSoundStagePiece& Piece : Stage->Pieces)
	{
		if (Piece.StreamOrder != Order) continue;
		if (ASplatPakPedestal* Pedestal = GetPiece(Piece.Name)) { Pedestal->SculptureComponent->SetStreamHold(false); }
		UE_LOG(LogSoundStage, Log, TEXT("%s: streaming %s (order %d) after %.1f s"), *GetName(), *Piece.Name.ToString(), Order, StreamStepSeconds);
	}
}

// Each step waits for its scans' views to load (or 20 s, so a view that never fills does not hold the rest back), then
// releases the next: the backdrop's view, then each stream order of pieces, then the rest of the backdrop
void ASoundStageActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateHiddenPieces();
	if (StreamStep == INDEX_NONE || !Stage) return;
	StreamStepSeconds += DeltaSeconds;
	StreamCheckTimer -= DeltaSeconds;
	if (StreamCheckTimer > 0.0f) return;
	StreamCheckTimer = 0.25f;

	bool bLoaded = true;
	if (StreamStep == 0)
	{
		bLoaded = BackdropComponent->IsViewLoaded(0.75f); // Enough of the first view to start on the pieces
	}
	else
	{
		for (const FSoundStagePiece& Piece : Stage->Pieces)
		{
			ASplatPakPedestal* Pedestal = Piece.StreamOrder == StreamOrders[StreamStep - 1] ? GetPiece(Piece.Name) : nullptr;
			if (Pedestal && Pedestal->Sculpture && !Pedestal->SculptureComponent->IsViewLoaded(0.9f, true)) bLoaded = false; // A piece: its finest needed level
		}
	}
	// The first step is held to 2 s: the stairwell's coarse view is in by then, and the first piece needs the time to
	// reach its finest level by the top of the stairs (about 18 MB/s each, unthrottled, in play)
	if (!bLoaded && StreamStepSeconds < (StreamStep == 0 ? 2.0f : 20.0f)) return;

	if (StreamStep < StreamOrders.Num())
	{
		ReleaseStreamOrder(StreamOrders[StreamStep]);
		StreamStep++;
	}
	else
	{
		BackdropComponent->bGreedyEnvironment = true;
		UE_LOG(LogSoundStage, Log, TEXT("%s: streaming the rest of the backdrop after %.1f s"), *GetName(), StreamStepSeconds);
		StreamStep = INDEX_NONE;
	}
	StreamStepSeconds = 0.0f;
}

void ASoundStageActor::ReconnectComponents()
{
	if (!Stage) { return; }
	TInlineComponentArray<UActorComponent*> Components(this);
	auto Find = [&Components](const FString& Wanted) -> UActorComponent*
	{
		for (UActorComponent* Component : Components)
		{
			if (Component && Component->GetFName().GetPlainNameString() == Wanted) { return Component; }
		}
		return nullptr;
	};
	for (const FSoundStageVolume& Volume : Stage->Volumes)
	{
		if (UShapeComponent* Shape = Cast<UShapeComponent>(Find(TEXT("Volume_") + Volume.Name.ToString()))) { VolumeComponents.Add(Volume.Name, Shape); }
	}
	for (const FSoundStagePiece& Piece : Stage->Pieces)
	{
		if (UChildActorComponent* Child = Cast<UChildActorComponent>(Find(TEXT("Piece_") + Piece.Name.ToString()))) { PieceComponents.Add(Piece.Name, Child); }
	}
	BoundaryFaces.Reset();
	for (UActorComponent* Component : Components)
	{
		if (UBoxComponent* Face = Cast<UBoxComponent>(Component); Face && Face->GetFName().GetPlainNameString() == TEXT("BoundaryFace")) { BoundaryFaces.Add(Face); }
	}
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
	ApplyPiece(Child, Piece);
	PieceComponents.Add(Piece.Name, Child);
}

void ASoundStageActor::ApplyPiece(UChildActorComponent* Child, const FSoundStagePiece& Piece) const
{
	if (ASplatPakPedestal* Pedestal = Child ? Cast<ASplatPakPedestal>(Child->GetChildActor()) : nullptr)
	{
		Pedestal->Sculpture = Piece.Sculpture;
		Pedestal->SculptureHeight = Piece.Height * 100.0f;
		Pedestal->FrontYawOffset = Piece.FrontYawOffset;
		Pedestal->PlinthMesh = Piece.Plinth;
		Pedestal->PlinthEmissive = Piece.PlinthEmissive;
		Pedestal->bUplight = Piece.bUplight;
		Pedestal->bSculptureAlwaysFinest = Piece.bAlwaysFinest;
		Pedestal->bDropShadow = Piece.bDropShadow && Stage;
		if (Stage)
		{
			// The package frame's direction (Y up) in the stage's frame (Z up), then the world's
			const FVector D = Piece.DropShadowDirection.IsNearlyZero() ? Stage->DropShadowDirection : Piece.DropShadowDirection;
			Pedestal->DropShadowDirection = GetActorTransform().TransformVectorNoScale(FVector(D.X, D.Z, D.Y)).GetSafeNormal();
			Pedestal->DropShadowOpacity = Stage->DropShadowOpacity;
			Pedestal->ColourMatchScene = BackdropComponent;
			Pedestal->ColourMatch = Piece.ColourMatch >= 0.0f ? Piece.ColourMatch : Stage->ColourMatchStrength;
			Pedestal->ColourMatchRadius = Stage->ColourMatchRadius * 100.0f;
		}
		Pedestal->UpdateLayout();
	}
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
