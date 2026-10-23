// SurfelAsset.cpp

#include "SurfelAsset.h"

#include "Misc/ScopeLock.h"
#include "SurfelPackageCore.h"

USurfelAsset::USurfelAsset()
{
	// The payload can be hundreds of MB: keep it out of the export so loading the asset (e.g. in
	// the Content Browser) doesn't read it until a level is decoded.
	PackageData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
}

bool USurfelAsset::SetPackageData(TArrayView<const uint8> Bytes, FString& OutError)
{
	SurfelsCore::FPackageInfo Info;
	std::string Error;
	if (!SurfelsCore::ReadPackageInfo(Bytes.GetData(), Bytes.Num(), Info, Error))
	{
		OutError = UTF8_TO_TCHAR(Error.c_str());
		return false;
	}

	PackageData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(PackageData.Realloc(Bytes.Num()), Bytes.GetData(), Bytes.Num());
	PackageData.Unlock();

	FormatVersion = (int32)Info.Header.Version;
	RecordFormat = (Info.Format == SurfelsCore::ERecordFormat::Splat20) ? TEXT("Splat20") : TEXT("Surfel8");
	NumChunks = (int32)Info.Chunks.size();
	CoarsestLevel = (int32)Info.MaxLevel;
	TotalSurfelsLOD0 = (int64)Info.Header.TotalSurfelsLOD0;
	BoundsMin = FVector3f(Info.Header.BoundsMin.X, Info.Header.BoundsMin.Y, Info.Header.BoundsMin.Z);
	BoundsMax = FVector3f(Info.Header.BoundsMax.X, Info.Header.BoundsMax.Y, Info.Header.BoundsMax.Z);

	FScopeLock Lock(&CacheLock);
	DecodedLevels.Reset();
	return true;
}

FSurfelLevelPtr USurfelAsset::GetDecodedLevel(int32 Level, FString* OutError)
{
	const int32 Effective = (Level < 0 || Level > CoarsestLevel) ? CoarsestLevel : Level;

	FScopeLock Lock(&CacheLock);
	if (const FSurfelLevelPtr* Cached = DecodedLevels.Find(Effective))
	{
		return *Cached;
	}

	const int64 Size = PackageData.GetBulkDataSize();
	if (Size == 0)
	{
		if (OutError) *OutError = TEXT("The asset holds no package data; re-import the .sflw file.");
		return nullptr;
	}

	// Step 1 decodes a whole level synchronously; streaming (step 4) replaces this with chunk reads.
	const uint8* Data = static_cast<const uint8*>(PackageData.LockReadOnly());
	SurfelsCore::FPackageInfo Info;
	TSharedPtr<SurfelsCore::FDecodedLevel, ESPMode::ThreadSafe> Decoded = MakeShared<SurfelsCore::FDecodedLevel, ESPMode::ThreadSafe>();
	std::string Error;
	const bool bOk = SurfelsCore::ReadPackageInfo(Data, Size, Info, Error)
		&& SurfelsCore::DecodeLevel(Data, Size, Info, (uint32)Effective, *Decoded, Error);
	PackageData.Unlock();

	if (!bOk)
	{
		if (OutError) *OutError = UTF8_TO_TCHAR(Error.c_str());
		return nullptr;
	}
	DecodedLevels.Add(Effective, Decoded);
	return Decoded;
}

void USurfelAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	PackageData.Serialize(Ar, this);

	if (Ar.IsLoading())
	{
		FScopeLock Lock(&CacheLock);
		DecodedLevels.Reset();
	}
}
