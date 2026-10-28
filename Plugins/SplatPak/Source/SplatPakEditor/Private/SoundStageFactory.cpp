// SoundStageFactory.cpp

#include "SoundStageFactory.h"

#include "AssetTypeCategories.h"
#include "Misc/FeedbackContext.h"
#include "SoundStageAsset.h"

USoundStageFactory::USoundStageFactory()
{
	SupportedClass = USoundStageAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = true;
	bText = false;
	Formats.Add(TEXT("json;SoundStage scene spec (*.soundstage.json)"));
}

UObject* USoundStageFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<USoundStageAsset>(InParent, InClass, InName, Flags | RF_Transactional);
}

UObject* USoundStageFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename,
	const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	USoundStageAsset* Stage = NewObject<USoundStageAsset>(InParent, InClass, InName, Flags | RF_Transactional);
	FString Error;
	if (!Stage->ImportSpecFile(Filename, Error))
	{
		Warn->Logf(ELogVerbosity::Warning, TEXT("%s: %s"), *Filename, *Error);
	}
	return Stage;
}

bool USoundStageFactory::FactoryCanImport(const FString& Filename)
{
	return Filename.EndsWith(TEXT(".soundstage.json"), ESearchCase::IgnoreCase); // Not every JSON file
}

FText USoundStageFactory::GetDisplayName() const
{
	return NSLOCTEXT("SplatPak", "SoundStageFactory", "SoundStage");
}

uint32 USoundStageFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Misc;
}
