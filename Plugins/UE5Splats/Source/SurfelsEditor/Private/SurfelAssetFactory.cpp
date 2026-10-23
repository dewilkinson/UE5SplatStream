// SurfelAssetFactory.cpp

#include "SurfelAssetFactory.h"

#include "Misc/FeedbackContext.h"
#include "SurfelAsset.h"

USurfelAssetFactory::USurfelAssetFactory()
{
	SupportedClass = USurfelAsset::StaticClass();
	Formats.Add(TEXT("sflw;Surfels package"));
	bCreateNew = false;
	bEditorImport = true;
	bText = false;
}

UObject* USurfelAssetFactory::FactoryCreateBinary(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context,
	const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn)
{
	USurfelAsset* Asset = NewObject<USurfelAsset>(InParent, InClass, InName, Flags | RF_Transactional);

	FString Error;
	if (!Asset->SetPackageData(TArrayView<const uint8>(Buffer, (int32)(BufferEnd - Buffer)), Error))
	{
		Warn->Logf(ELogVerbosity::Error, TEXT("Could not import %s: %s"), *GetCurrentFilename(), *Error);
		return nullptr;
	}
	Asset->SourceFilename = GetCurrentFilename();
	return Asset;
}
