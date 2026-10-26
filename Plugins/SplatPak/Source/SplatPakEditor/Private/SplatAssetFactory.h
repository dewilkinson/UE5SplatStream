// SplatAssetFactory.h
// Imports .gstx packages (and older .sflw ones) (drag into the Content Browser, or Import) as USplatAsset.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SplatAssetFactory.generated.h"

UCLASS()
class USplatAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	USplatAssetFactory();

	virtual UObject* FactoryCreateBinary(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context,
		const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn) override;

	// Builds a static mesh from the asset's shadow mesh (one LOD per level, in the default import frame: metres to
	// centimetres, Y-up to Z-up; distance field and complex-as-simple collision on, Nanite off) in Outer. Null when the
	// package has none. The importer puts it in a package beside the asset; tests use the transient package.
	static UStaticMesh* BuildShadowMesh(class USplatAsset* Asset, UObject* Outer, FName MeshName, EObjectFlags Flags);
};
