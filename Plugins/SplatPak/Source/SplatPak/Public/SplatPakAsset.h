// SplatPakAsset.h
// An imported .gstx package. The file is stored whole as bulk data (so it cooks and packages like
// any asset); levels are decoded on demand and cached for every component that shows them.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Serialization/BulkData.h"
#include "UObject/Object.h"
#include <vector>
#include "SplatPakAsset.generated.h"

namespace SplatCore { struct FDecodedLevel; struct FShadowMeshLevel; }
class UStaticMesh;
using FSplatPakLevelPtr = TSharedPtr<const SplatCore::FDecodedLevel, ESPMode::ThreadSafe>;
struct FSplatPakStreamLevels;
using FSplatPakStreamLevelsPtr = TSharedPtr<const FSplatPakStreamLevels, ESPMode::ThreadSafe>;

// One camera the scan was trained from (the package's view settings, GSTXVEW1), in the package's model frame: the view
// its published renders are made from. USplatPakComponent::GetTrainingCamera places it in the world.
USTRUCT(BlueprintType)
struct SPLATPAK_API FSplatPakTrainingCamera
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak")
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak")
	FVector3f Eye = FVector3f::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak")
	FVector3f Forward = FVector3f(0, 0, -1);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak")
	FVector3f Up = FVector3f(0, 1, 0);

	// Vertical field of view, degrees
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak")
	float FovYDegrees = 45.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak")
	FIntPoint PhotoSize = FIntPoint(0, 0);
};

UCLASS(BlueprintType, hidecategories = Object)
class SPLATPAK_API USplatPakAsset : public UObject
{
	GENERATED_BODY()

public:
	USplatPakAsset();

	// Replaces the package with Bytes after validating them. Used by the importer.
	bool SetPackageData(TArrayView<const uint8> Bytes, FString& OutError);

	// Decodes (or returns the cached) level. Level < 0 or past the coarsest means the coarsest.
	FSplatPakLevelPtr GetDecodedLevel(int32 Level, FString* OutError = nullptr);

	// Every level, as the streaming runtime takes them (SplatPakStreamLevels.h). Game thread. The first call starts the
	// build on a worker thread (a few seconds for a large scan); null until it is ready, or when it failed (OutError).
	FSplatPakStreamLevelsPtr GetStreamLevels(FString* OutError = nullptr);

	FBox GetModelBounds() const { return FBox(FVector(BoundsMin), FVector(BoundsMax)); }

	// The package's shadow mesh levels (format v12), finest first; false when it has none. Used by the importer.
	bool ReadShadowMeshLevels(std::vector<SplatCore::FShadowMeshLevel>& OutLevels, FString* OutError = nullptr);
	int64 GetPackageBytes() const { return PackageData.GetBulkDataSize(); }

	virtual void Serialize(FArchive& Ar) override;

	UPROPERTY(VisibleAnywhere, Category = "SplatPak")
	FString SourceFilename;

	UPROPERTY(VisibleAnywhere, Category = "SplatPak")
	int32 FormatVersion = 0;

	// "Point8" (oriented discs) or "Splat20" (3D Gaussians)
	UPROPERTY(VisibleAnywhere, Category = "SplatPak")
	FString RecordFormat;

	UPROPERTY(VisibleAnywhere, Category = "SplatPak")
	int32 NumChunks = 0;

	// The coarsest LOD level in the package (0 is the finest).
	UPROPERTY(VisibleAnywhere, Category = "SplatPak")
	int32 CoarsestLevel = 0;

	UPROPERTY(VisibleAnywhere, Category = "SplatPak")
	int64 TotalSplatsLOD0 = 0;

	// Model-space bounds, in the package's own frame and units.
	UPROPERTY(VisibleAnywhere, Category = "SplatPak")
	FVector3f BoundsMin = FVector3f::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "SplatPak")
	FVector3f BoundsMax = FVector3f::ZeroVector;

	// The static mesh built from the package's shadow mesh at import (<asset>_ShadowMesh, three LODs, in the default
	// import frame: package metres to centimetres, Y-up to Z-up). Splat components place it hidden, for shadows, Lumen
	// and collision. Null for a package without one (format 11 or older).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak")
	TObjectPtr<UStaticMesh> ShadowMesh;

	// The package's view settings (GSTXVEW1; defaults for a package without them). An environment is a capture of a place
	// (a dense core inside a far wider shell): it streams by screen-space error with a residency ring around the eye.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak|View")
	bool bEnvironmentScene = false;

	// Radius about the core holding half the finest level, package metres (0 = not stated)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak|View")
	float CoreRadius = 0.0f;

	// The training camera to open at (-1 = none)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak|View")
	int32 StartCamera = -1;

	// What the scan is of (where its training cameras converge, out to where the density falls away), package frame and
	// metres; streamed first. Radius 0 = not stated.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak|View")
	FVector3f SubjectCentre = FVector3f::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak|View")
	float SubjectRadius = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SplatPak|View")
	TArray<FSplatPakTrainingCamera> TrainingCameras;

private:
	FByteBulkData PackageData;

	FCriticalSection CacheLock;
	TMap<int32, FSplatPakLevelPtr> DecodedLevels;

	FSplatPakStreamLevelsPtr StreamLevels;
	TFuture<FSplatPakStreamLevelsPtr> StreamLevelsBuild;
	TSharedPtr<FString, ESPMode::ThreadSafe> StreamLevelsError;
};
