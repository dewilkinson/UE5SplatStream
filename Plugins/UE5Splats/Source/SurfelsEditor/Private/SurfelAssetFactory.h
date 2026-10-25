// SurfelAssetFactory.h
// Imports .gstx packages (and older .sflw ones) (drag into the Content Browser, or Import) as USurfelAsset.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SurfelAssetFactory.generated.h"

UCLASS()
class USurfelAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	USurfelAssetFactory();

	virtual UObject* FactoryCreateBinary(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context,
		const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn) override;
};
