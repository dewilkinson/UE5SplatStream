// SplatAsset.h
// An imported .gstx package. The file is stored whole as bulk data (so it cooks and packages like
// any asset); levels are decoded on demand and cached for every component that shows them.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/BulkData.h"
#include "UObject/Object.h"
#include <vector>
#include "SplatAsset.generated.h"

namespace SplatCore { struct FDecodedLevel; struct FShadowMeshLevel; }
class UStaticMesh;
using FSplatLevelPtr = TSharedPtr<const SplatCore::FDecodedLevel, ESPMode::ThreadSafe>;

UCLASS(BlueprintType, hidecategories = Object)
class SPLATPAK_API USplatAsset : public UObject
{
	GENERATED_BODY()

public:
	USplatAsset();

	// Replaces the package with Bytes after validating them. Used by the importer.
	bool SetPackageData(TArrayView<const uint8> Bytes, FString& OutError);

	// Decodes (or returns the cached) level. Level < 0 or past the coarsest means the coarsest.
	FSplatLevelPtr GetDecodedLevel(int32 Level, FString* OutError = nullptr);

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

private:
	FByteBulkData PackageData;

	FCriticalSection CacheLock;
	TMap<int32, FSplatLevelPtr> DecodedLevels;
};
