// SoundStageFactory.h
// Makes SoundStage assets: new from the Content Browser (an empty stage), or imported from a scene spec
// (*.soundstage.json, dragged in or through Import), its paths resolved relative to the folder it is imported into.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "SoundStageFactory.generated.h"

UCLASS()
class USoundStageFactory : public UFactory
{
	GENERATED_BODY()

public:
	USoundStageFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename,
		const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
};
