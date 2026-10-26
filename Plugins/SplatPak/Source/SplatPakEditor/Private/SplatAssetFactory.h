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
};
