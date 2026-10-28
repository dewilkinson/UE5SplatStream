// SoundStageActor.h
// Realises a SoundStage (USoundStageAsset) in a level: the backdrop scan, one collision shape per volume (drawn as a
// wireframe in the volume kind's colour), and a pedestal (ASplatPakPedestal) per piece. The stage's package frame
// (metres, Y up) maps into this actor's frame as centimetres, Z up, as a splat component maps its scan. It rebuilds when
// the asset changes, and its accessors look things up by name and test points against the volumes.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SoundStageAsset.h"
#include "SoundStageActor.generated.h"

class ASplatPakPedestal;
class UChildActorComponent;
class UShapeComponent;
class USplatPakComponent;

UCLASS(ClassGroup = Rendering)
class SPLATPAK_API ASoundStageActor : public AActor
{
	GENERATED_BODY()

public:
	ASoundStageActor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SoundStage")
	TObjectPtr<USoundStageAsset> Stage;

	// The volumes as collision (off: they are only drawn)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SoundStage")
	bool bVolumeCollision = true;

	// Draw the volumes' wireframes in the editor, and in play when bShowVolumesInGame is on too
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SoundStage")
	bool bShowVolumes = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SoundStage", meta = (EditCondition = "bShowVolumes"))
	bool bShowVolumesInGame = false;

	// What each piece is placed with (a Blueprint of it can restyle every piece)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SoundStage")
	TSubclassOf<ASplatPakPedestal> PedestalClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SoundStage")
	TObjectPtr<USplatPakComponent> BackdropComponent;

	// ---- Accessors ----

	// Rebuilds everything from the asset (it also runs when the asset changes)
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "SoundStage")
	void Rebuild();

	UFUNCTION(BlueprintPure, Category = "SoundStage")
	UShapeComponent* GetVolumeComponent(FName Name) const;

	UFUNCTION(BlueprintPure, Category = "SoundStage")
	ASplatPakPedestal* GetPiece(FName Name) const;

	UFUNCTION(BlueprintPure, Category = "SoundStage")
	TArray<FName> GetPieceNames() const;

	// The volumes containing a world point
	UFUNCTION(BlueprintPure, Category = "SoundStage")
	TArray<FName> GetVolumesAt(FVector WorldLocation) const;

	// Inside every boundary volume (true when the stage has none)
	UFUNCTION(BlueprintPure, Category = "SoundStage")
	bool IsInsideBoundary(FVector WorldLocation) const;

	// The visitor's start: the eye's world position, looking along the stage's StartLook
	UFUNCTION(BlueprintPure, Category = "SoundStage")
	FTransform GetStartTransform() const;

	UFUNCTION(BlueprintPure, Category = "SoundStage")
	FVector PackageToWorld(FVector PackagePoint) const;

	UFUNCTION(BlueprintPure, Category = "SoundStage")
	FVector WorldToPackage(FVector WorldLocation) const;

	UFUNCTION(BlueprintCallable, Category = "SoundStage")
	void SetVolumesVisible(bool bVisible);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

private:
	void Clear();
	void AddVolume(const FSoundStageVolume& Volume);
	void AddPiece(const FSoundStagePiece& Piece);
	UShapeComponent* MakeShape(const FSoundStageVolume& Volume, FName ComponentName);
	void WatchStage();
	void OnStageChanged(USoundStageAsset* Changed);

	// The shape drawn for each volume (a boundary's blocking faces are in BoundaryFaces)
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UShapeComponent>> VolumeComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UShapeComponent>> BoundaryFaces;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UChildActorComponent>> PieceComponents;

	TWeakObjectPtr<USoundStageAsset> WatchedStage;
	FDelegateHandle StageChangedHandle;
};
