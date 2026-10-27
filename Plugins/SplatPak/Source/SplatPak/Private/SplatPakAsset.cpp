// SplatPakAsset.cpp

#include "SplatPakAsset.h"

#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "SplatPakPackageCore.h"
#include "SplatPakStreamLevels.h"
#include <PackageView.h>

USplatPakAsset::USplatPakAsset()
{
	// The payload can be hundreds of MB: keep it out of the export so loading the asset (e.g. in
	// the Content Browser) doesn't read it until a level is decoded.
	PackageData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
}

bool USplatPakAsset::SetPackageData(TArrayView<const uint8> Bytes, FString& OutError)
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

	// The view settings (GSTXVEW1), when the package carries them
	{
		Splat::PackageView View;
		bEnvironmentScene = false;
		CoreRadius = 0.0f;
		StartCamera = -1;
		SubjectRadius = 0.0f;
		TrainingCameras.Reset();
		if (View.ReadMemory(Bytes.GetData(), (size_t)Bytes.Num()))
		{
			bEnvironmentScene = (View.scene == "environment");
			CoreRadius = View.coreRadius50;
			StartCamera = View.startCamera;
			SubjectCentre = FVector3f(View.subjectCentre.x, View.subjectCentre.y, View.subjectCentre.z);
			SubjectRadius = View.subjectRadius;
			for (const Splat::TrainingCamera& C : View.cameras)
			{
				FSplatPakTrainingCamera& T = TrainingCameras.AddDefaulted_GetRef();
				T.Name = UTF8_TO_TCHAR(C.name.c_str());
				T.Eye = FVector3f(C.eye.x, C.eye.y, C.eye.z);
				T.Forward = FVector3f(C.forward.x, C.forward.y, C.forward.z);
				T.Up = FVector3f(C.up.x, C.up.y, C.up.z);
				T.FovYDegrees = FMath::RadiansToDegrees(C.fovY);
				T.PhotoSize = FIntPoint((int32)C.width, (int32)C.height);
			}
		}
	}

	FScopeLock Lock(&CacheLock);
	DecodedLevels.Reset();
	StreamLevels.Reset();
	StreamLevelsError.Reset();
	return true;
}

FSplatPakLevelPtr USplatPakAsset::GetDecodedLevel(int32 Level, FString* OutError)
{
	const int32 Effective = (Level < 0 || Level > CoarsestLevel) ? CoarsestLevel : Level;

	FScopeLock Lock(&CacheLock);
	if (const FSplatPakLevelPtr* Cached = DecodedLevels.Find(Effective))
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

FSplatPakStreamLevelsPtr USplatPakAsset::GetStreamLevels(FString* OutError)
{
	check(IsInGameThread());
	if (StreamLevels.IsValid()) return StreamLevels;
	if (StreamLevelsBuild.IsValid())
	{
		if (!StreamLevelsBuild.IsReady()) return nullptr;
		StreamLevels = StreamLevelsBuild.Get();
		StreamLevelsBuild = TFuture<FSplatPakStreamLevelsPtr>();
		if (!StreamLevels.IsValid() && OutError && StreamLevelsError.IsValid()) *OutError = *StreamLevelsError;
		return StreamLevels;
	}
	if (StreamLevelsError.IsValid())
	{
		if (OutError) *OutError = *StreamLevelsError; // Built once and failed: not again
		return nullptr;
	}

	// The package bytes are copied here, on the game thread, so the worker never touches the bulk data
	const int64 Size = PackageData.GetBulkDataSize();
	StreamLevelsError = MakeShared<FString, ESPMode::ThreadSafe>();
	if (Size == 0)
	{
		*StreamLevelsError = TEXT("The asset holds no package data; re-import the .gstx file.");
		if (OutError) *OutError = *StreamLevelsError;
		return nullptr;
	}
	TSharedRef<TArray64<uint8>, ESPMode::ThreadSafe> Bytes = MakeShared<TArray64<uint8>, ESPMode::ThreadSafe>();
	Bytes->SetNumUninitialized(Size);
	FMemory::Memcpy(Bytes->GetData(), PackageData.LockReadOnly(), Size);
	PackageData.Unlock();
	TSharedPtr<FString, ESPMode::ThreadSafe> Error = StreamLevelsError;
	StreamLevelsBuild = Async(EAsyncExecution::ThreadPool, [Bytes, Error]() -> FSplatPakStreamLevelsPtr
	{
		TSharedPtr<FSplatPakStreamLevels, ESPMode::ThreadSafe> Levels = MakeShared<FSplatPakStreamLevels, ESPMode::ThreadSafe>();
		if (!BuildSplatStreamLevels(Bytes->GetData(), (uint64)Bytes->Num(), *Levels, *Error)) return nullptr;
		return Levels;
	});
	return nullptr;
}

bool USplatPakAsset::ReadShadowMeshLevels(std::vector<SplatCore::FShadowMeshLevel>& OutLevels, FString* OutError)
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

void USplatPakAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	PackageData.Serialize(Ar, this);

	if (Ar.IsLoading())
	{
		FScopeLock Lock(&CacheLock);
		DecodedLevels.Reset();
		StreamLevels.Reset();
		StreamLevelsError.Reset();
	}
}
