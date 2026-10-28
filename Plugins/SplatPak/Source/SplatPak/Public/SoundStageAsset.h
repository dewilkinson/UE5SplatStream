// SoundStageAsset.h
// A SoundStage: a scene built on a splat scan, the way a film set is built on a sound stage. It holds the scan as the
// backdrop, the volumes that make the set solid (floors, walls, stairs, railings, and the boundary the visitor stays
// inside), the pieces placed in it (sculptures on plinths), and where the visitor starts. ASoundStageActor realises one in
// a level.
//
// Everything in it is in the backdrop package's own frame: metres, Y up (SplatPak's frame), so the same scene spec
// (*.soundstage.json, docs/soundstage.md) can be read by SplatPak as well as here. ImportSpec/ExportSpec convert between
// the two; PackageToStage maps a package-frame point into the stage actor's frame (centimetres, Z up).
//
// The stage names its splats (and plinths) by path relative to a root directory: "Palace/bishops_palace" under the root
// "/Game" here, the same relative path under SplatPak's assets folder there. The object pointers beside the paths are
// those paths resolved (ResolveAssets), and what the stage actor draws.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SoundStageAsset.generated.h"

class USplatPakAsset;
class UStaticMesh;

// What a volume is for: how it collides and the colour it is drawn in
UENUM(BlueprintType)
enum class ESoundStageVolumeKind : uint8
{
	Floor    UMETA(ToolTip = "Walkable: blocks, drawn green"),
	Wall     UMETA(ToolTip = "Blocks, drawn white"),
	Stairs   UMETA(ToolTip = "A walkable ramp over a flight of stairs (tilt it with Pitch): blocks, drawn orange"),
	Railing  UMETA(ToolTip = "Banisters and balustrades: blocks, drawn yellow"),
	Boundary UMETA(ToolTip = "The space the visitor stays inside: its faces block from within, drawn red"),
	Trigger  UMETA(ToolTip = "Overlaps only, for gameplay (a room, a hotspot): drawn cyan"),
};

UENUM(BlueprintType)
enum class ESoundStageShape : uint8
{
	Box,
	Sphere  UMETA(ToolTip = "Size.X is the diameter"),
	Capsule UMETA(ToolTip = "Upright: Size.X is the diameter, Size.Y the full height"),
};

USTRUCT(BlueprintType)
struct SPLATPAK_API FSoundStageVolume
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	ESoundStageVolumeKind Kind = ESoundStageVolumeKind::Wall;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	ESoundStageShape Shape = ESoundStageShape::Box;

	// Centre, package frame (metres, Y up)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	FVector Center = FVector::ZeroVector;

	// Full size along the shape's own axes, metres: X along its heading, Y up, Z across
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	FVector Size = FVector(1.0);

	// Heading of the shape's X axis, degrees from the package's +X toward +Z (the stage's yaw)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	float Yaw = 0.0f;

	// Tilt, degrees: positive raises the +X end (a flight of stairs climbing along its heading)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	float Pitch = 0.0f;
};

USTRUCT(BlueprintType)
struct SPLATPAK_API FSoundStagePiece
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	FName Name;

	// Relative to the stage's root directory, e.g. "Palace/oligarchy"
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	FString SculpturePath;

	// SculpturePath resolved
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	TObjectPtr<USplatPakAsset> Sculpture;

	// The point on the floor the piece stands on, package frame (metres, Y up)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	FVector Floor = FVector::ZeroVector;

	// The way it faces, degrees from the package's +X toward +Z
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	float Facing = 0.0f;

	// The sculpture's height on its plinth, metres
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage", meta = (ClampMin = "0.01"))
	float Height = 1.0f;

	// Degrees added to the sculpture's yaw, for a scan whose front is not its +X
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	float FrontYawOffset = 0.0f;

	// Relative to the root directory; empty: the sculpture stands on the floor
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	FString PlinthPath;

	// PlinthPath resolved
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	TObjectPtr<UStaticMesh> Plinth;

	// Plinth glow (ASplatPakPedestal::PlinthEmissive)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage", meta = (ClampMin = "0.0"))
	float PlinthEmissive = 0.3f;

	// A spot on the floor in front, casting the piece's shadow up the wall behind it
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	bool bUplight = false;
};

UCLASS(BlueprintType, hidecategories = Object)
class SPLATPAK_API USoundStageAsset : public UObject
{
	GENERATED_BODY()

public:
	// The directory the stage's asset paths are relative to (a content path, e.g. "/Game"). Empty: the folder this asset is in.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	FString RootDirectory;

	// The scan the stage is built on, relative to the root directory
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	FString BackdropPath;

	// BackdropPath resolved
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage")
	TObjectPtr<USplatPakAsset> Backdrop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage", meta = (TitleProperty = "Name"))
	TArray<FSoundStageVolume> Volumes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage", meta = (TitleProperty = "Name"))
	TArray<FSoundStagePiece> Pieces;

	// Where the visitor starts: eye position and the direction they look, package frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage|Start")
	FVector StartEye = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundStage|Start")
	FVector StartLook = FVector(0, 0, -1);

	// The spec file this was last imported from or exported to
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SoundStage")
	FString SpecFile;

	// ---- Accessors ----

	UFUNCTION(BlueprintPure, Category = "SoundStage")
	bool FindVolume(FName Name, FSoundStageVolume& OutVolume) const;

	UFUNCTION(BlueprintPure, Category = "SoundStage")
	TArray<FSoundStageVolume> GetVolumesOfKind(ESoundStageVolumeKind Kind) const;

	// Adds the volume, or replaces the one with its name
	UFUNCTION(BlueprintCallable, Category = "SoundStage")
	void SetVolume(const FSoundStageVolume& Volume);

	UFUNCTION(BlueprintCallable, Category = "SoundStage")
	bool RemoveVolume(FName Name);

	UFUNCTION(BlueprintPure, Category = "SoundStage")
	bool FindPiece(FName Name, FSoundStagePiece& OutPiece) const;

	// Adds the piece, or replaces the one with its name
	UFUNCTION(BlueprintCallable, Category = "SoundStage")
	void SetPiece(const FSoundStagePiece& Piece);

	UFUNCTION(BlueprintCallable, Category = "SoundStage")
	bool RemovePiece(FName Name);

	// ---- Paths ----

	// The root directory in effect: RootDirectory, or this asset's folder
	UFUNCTION(BlueprintPure, Category = "SoundStage|Paths")
	FString GetRoot() const;

	// The asset at a path relative to the root (null when there is none)
	UFUNCTION(BlueprintCallable, Category = "SoundStage|Paths")
	UObject* ResolvePath(const FString& RelativePath) const;

	// An asset's path relative to the root (with ".." when it is outside it); empty for none
	UFUNCTION(BlueprintPure, Category = "SoundStage|Paths")
	FString MakeRelativePath(const UObject* Asset) const;

	// Resolves every path to its asset; a path left empty is filled from its asset. False, with the paths that matched
	// nothing in OutMissing, when one is missing.
	UFUNCTION(BlueprintCallable, Category = "SoundStage|Paths")
	bool ResolveAssets(TArray<FString>& OutMissing);

	// ---- Frames ----

	// A package-frame point (metres, Y up) in the stage's frame (centimetres, Z up)
	UFUNCTION(BlueprintPure, Category = "SoundStage")
	static FVector PackageToStage(FVector Point) { return FVector(Point.X, Point.Z, Point.Y) * 100.0; }

	UFUNCTION(BlueprintPure, Category = "SoundStage")
	static FVector StageToPackage(FVector Point) { return FVector(Point.X, Point.Z, Point.Y) / 100.0; }

	// A volume's placement in the stage's frame: its rotation and centre (unscaled; Size gives the extents)
	UFUNCTION(BlueprintPure, Category = "SoundStage")
	static FTransform GetVolumeTransform(const FSoundStageVolume& Volume);

	// ---- The scene spec ----

	// Replaces everything but RootDirectory with the spec (JSON text), and resolves its paths. False, with OutError, when
	// the JSON is malformed or a path matches nothing (the rest is still imported).
	UFUNCTION(BlueprintCallable, Category = "SoundStage|Spec")
	bool ImportSpec(const FString& Json, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "SoundStage|Spec")
	bool ImportSpecFile(const FString& Filename, FString& OutError);

	// The spec as JSON text; assets are written as their relative paths
	UFUNCTION(BlueprintPure, Category = "SoundStage|Spec")
	FString ExportSpec() const;

	UFUNCTION(BlueprintCallable, Category = "SoundStage|Spec")
	bool ExportSpecFile(const FString& Filename, FString& OutError);

	// The format ImportSpec reads and ExportSpec writes
	static constexpr int32 SpecVersion = 1;

	// Broadcast after an import or an edit, so stage actors showing it rebuild
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnStageChanged, USoundStageAsset*);
	FOnStageChanged OnStageChanged;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
};
