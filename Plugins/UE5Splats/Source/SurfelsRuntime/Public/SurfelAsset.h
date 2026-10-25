// SurfelAsset.h
// An imported .gstx package. The file is stored whole as bulk data (so it cooks and packages like
// any asset); levels are decoded on demand and cached for every component that shows them.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/BulkData.h"
#include "UObject/Object.h"
#include "SurfelAsset.generated.h"

namespace SurfelsCore { struct FDecodedLevel; }
using FSurfelLevelPtr = TSharedPtr<const SurfelsCore::FDecodedLevel, ESPMode::ThreadSafe>;

UCLASS(BlueprintType, hidecategories = Object)
class SURFELSRUNTIME_API USurfelAsset : public UObject
{
	GENERATED_BODY()

public:
	USurfelAsset();

	// Replaces the package with Bytes after validating them. Used by the importer.
	bool SetPackageData(TArrayView<const uint8> Bytes, FString& OutError);

	// Decodes (or returns the cached) level. Level < 0 or past the coarsest means the coarsest.
	FSurfelLevelPtr GetDecodedLevel(int32 Level, FString* OutError = nullptr);

	FBox GetModelBounds() const { return FBox(FVector(BoundsMin), FVector(BoundsMax)); }
	int64 GetPackageBytes() const { return PackageData.GetBulkDataSize(); }

	virtual void Serialize(FArchive& Ar) override;

	UPROPERTY(VisibleAnywhere, Category = "Surfels")
	FString SourceFilename;

	UPROPERTY(VisibleAnywhere, Category = "Surfels")
	int32 FormatVersion = 0;

	// "Surfel8" (oriented discs) or "Splat20" (3D Gaussians)
	UPROPERTY(VisibleAnywhere, Category = "Surfels")
	FString RecordFormat;

	UPROPERTY(VisibleAnywhere, Category = "Surfels")
	int32 NumChunks = 0;

	// The coarsest LOD level in the package (0 is the finest).
	UPROPERTY(VisibleAnywhere, Category = "Surfels")
	int32 CoarsestLevel = 0;

	UPROPERTY(VisibleAnywhere, Category = "Surfels")
	int64 TotalSurfelsLOD0 = 0;

	// Model-space bounds, in the package's own frame and units.
	UPROPERTY(VisibleAnywhere, Category = "Surfels")
	FVector3f BoundsMin = FVector3f::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Surfels")
	FVector3f BoundsMax = FVector3f::ZeroVector;

private:
	FByteBulkData PackageData;

	FCriticalSection CacheLock;
	TMap<int32, FSurfelLevelPtr> DecodedLevels;
};
