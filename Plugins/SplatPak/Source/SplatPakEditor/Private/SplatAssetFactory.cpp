// SplatAssetFactory.cpp

#include "SplatAssetFactory.h"

#include "Misc/FeedbackContext.h"
#include "SplatAsset.h"

USplatAssetFactory::USplatAssetFactory()
{
	SupportedClass = USplatAsset::StaticClass();
	Formats.Add(TEXT("gstx;GSTx package"));
	Formats.Add(TEXT("sflw;GSTx package (older .sflw)"));
	bCreateNew = false;
	bEditorImport = true;
	bText = false;
}

UObject* USplatAssetFactory::FactoryCreateBinary(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context,
	const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn)
{
	USplatAsset* Asset = NewObject<USplatAsset>(InParent, InClass, InName, Flags | RF_Transactional);

	FString Error;
	if (!Asset->SetPackageData(TArrayView<const uint8>(Buffer, (int32)(BufferEnd - Buffer)), Error))
	{
		Warn->Logf(ELogVerbosity::Error, TEXT("Could not import %s: %s"), *GetCurrentFilename(), *Error);
		return nullptr;
	}
	Asset->SourceFilename = GetCurrentFilename();
	return Asset;
}
