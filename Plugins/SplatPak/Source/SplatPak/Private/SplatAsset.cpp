// SplatAsset.cpp

#include "SplatAsset.h"

#include "Misc/ScopeLock.h"
#include "SplatPackageCore.h"

USplatAsset::USplatAsset()
{
	// The payload can be hundreds of MB: keep it out of the export so loading the asset (e.g. in
	// the Content Browser) doesn't read it until a level is decoded.
	PackageData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
}

bool USplatAsset::SetPackageData(TArrayView<const uint8> Bytes, FString& OutError)
{
	SplatCore::FPackageInfo Info;
	std::string Error;
	if (!SplatCore::ReadPackageInfo(Bytes.GetData(), Bytes.Num(), Info, Error))
	{
		OutError = UTF8_TO_TCHAR(Error.c_str());
		return false;
	}

	PackageData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(PackageData.Realloc(Bytes.Num()), Bytes.GetData(), Bytes.Num());
	PackageData.Unlock();

	FormatVersion = (int32)Info.Header.Version;
	RecordFormat = (Info.Format == SplatCore::ERecordFormat::Splat20) ? TEXT("Splat20") : TEXT("Point8");
	NumChunks = (int32)Info.Chunks.size();
	CoarsestLevel = (int32)Info.MaxLevel;
	TotalSplatsLOD0 = (int64)Info.Header.TotalSplatsLOD0;
	BoundsMin = FVector3f(Info.Header.BoundsMin.X, Info.Header.BoundsMin.Y, Info.Header.BoundsMin.Z);
	BoundsMax = FVector3f(Info.Header.BoundsMax.X, Info.Header.BoundsMax.Y, Info.Header.BoundsMax.Z);

	FScopeLock Lock(&CacheLock);
	DecodedLevels.Reset();
	return true;
}

FSplatLevelPtr USplatAsset::GetDecodedLevel(int32 Level, FString* OutError)
{
	const int32 Effective = (Level < 0 || Level > CoarsestLevel) ? CoarsestLevel : Level;

	FScopeLock Lock(&CacheLock);
	if (const FSplatLevelPtr* Cached = DecodedLevels.Find(Effective))
	{
		return *Cached;
	}

	const int64 Size = PackageData.GetBulkDataSize();
	if (Size == 0)
	{
		if (OutError) *OutError = TEXT("The asset holds no package data; re-import the .gstx file.");
		return nullptr;
	}

	// Step 1 decodes a whole level synchronously; streaming (step 4) replaces this with chunk reads.
	const uint8* Data = static_cast<const uint8*>(PackageData.LockReadOnly());
	SplatCore::FPackageInfo Info;
	TSharedPtr<SplatCore::FDecodedLevel, ESPMode::ThreadSafe> Decoded = MakeShared<SplatCore::FDecodedLevel, ESPMode::ThreadSafe>();
	std::string Error;
	const bool bOk = SplatCore::ReadPackageInfo(Data, Size, Info, Error)
		&& SplatCore::DecodeLevel(Data, Size, Info, (uint32)Effective, *Decoded, Error);
	PackageData.Unlock();

	if (!bOk)
	{
		if (OutError) *OutError = UTF8_TO_TCHAR(Error.c_str());
		return nullptr;
	}
	DecodedLevels.Add(Effective, Decoded);
	return Decoded;
}

bool USplatAsset::ReadShadowMeshLevels(std::vector<SplatCore::FShadowMeshLevel>& OutLevels, FString* OutError)
{
	OutLevels.clear();
	const int64 Size = PackageData.GetBulkDataSize();
	if (Size == 0) return false;
	const uint8* Data = static_cast<const uint8*>(PackageData.LockReadOnly());
	SplatCore::FPackageInfo Info;
	std::string Error;
	bool bOk = SplatCore::ReadPackageInfo(Data, Size, Info, Error) && SplatCore::HasShadowMesh(Info) && SplatCore::ReadShadowMesh(Data, Size, Info, OutLevels, Error);
	PackageData.Unlock();
	if (!bOk && OutError) *OutError = UTF8_TO_TCHAR(Error.c_str());
	return bOk;
}

void USplatAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	PackageData.Serialize(Ar, this);

	if (Ar.IsLoading())
	{
		FScopeLock Lock(&CacheLock);
		DecodedLevels.Reset();
	}
}
