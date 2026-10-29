// SoundStageAsset.cpp

#include "SoundStageAsset.h"

#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SplatPakAsset.h"

namespace
{
	const TCHAR* KindNames[] = { TEXT("floor"), TEXT("wall"), TEXT("stairs"), TEXT("railing"), TEXT("boundary"), TEXT("trigger") };
	const TCHAR* ShapeNames[] = { TEXT("box"), TEXT("sphere"), TEXT("capsule") };

	template <typename E, int N>
	bool ParseEnum(const FString& S, const TCHAR* (&Names)[N], E& Out)
	{
		for (int i = 0; i < N; i++)
		{
			if (S.Equals(Names[i], ESearchCase::IgnoreCase)) { Out = (E)i; return true; }
		}
		return false;
	}

	bool ReadVector(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O->TryGetArrayField(Field, A) || A->Num() != 3) { return false; }
		Out = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
		return true;
	}

	TSharedPtr<FJsonValue> WriteVector(const FVector& V)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		for (int i = 0; i < 3; i++)
		{
			A.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V[i] * 1000.0) / 1000.0)); // Millimetres are plenty
		}
		return MakeShared<FJsonValueArray>(A);
	}

	// A heading: degrees, or a package-frame direction [x, z]
	float ReadHeading(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, float Default)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O->TryGetArrayField(Field, A) && A->Num() == 2)
		{
			return (float)FMath::RadiansToDegrees(FMath::Atan2((*A)[1]->AsNumber(), (*A)[0]->AsNumber()));
		}
		double D = Default;
		O->TryGetNumberField(Field, D);
		return (float)D;
	}

	// A path field resolved, or filled from its asset when it is empty
	template <typename T>
	void Resolve(const USoundStageAsset& Stage, FString& Path, TObjectPtr<T>& Asset, TArray<FString>& Missing)
	{
		if (Path.IsEmpty())
		{
			Path = Stage.MakeRelativePath(Asset);
			return;
		}
		Asset = Cast<T>(Stage.ResolvePath(Path));
		if (!Asset) { Missing.Add(Path); }
	}
}

// ---- Paths -------------------------------------------------------------------------------------------------------

FString USoundStageAsset::GetRoot() const
{
	FString Root = RootDirectory.IsEmpty() ? FPackageName::GetLongPackagePath(GetPackage()->GetName()) : RootDirectory;
	Root.RemoveFromEnd(TEXT("/"));
	return Root;
}

UObject* USoundStageAsset::ResolvePath(const FString& RelativePath) const
{
	if (RelativePath.IsEmpty()) { return nullptr; }
	FString Package = GetRoot() / RelativePath;
	FPaths::CollapseRelativeDirectories(Package);
	const FString Object = Package + TEXT(".") + FPaths::GetBaseFilename(Package);
	return LoadObject<UObject>(nullptr, *Object, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

FString USoundStageAsset::MakeRelativePath(const UObject* Asset) const
{
	if (!Asset) { return FString(); }
	FString Path = Asset->GetPackage()->GetName();
	FPaths::MakePathRelativeTo(Path, *(GetRoot() + TEXT("/")));
	return Path;
}

bool USoundStageAsset::ResolveAssets(TArray<FString>& OutMissing)
{
	OutMissing.Reset();
	Resolve(*this, BackdropPath, Backdrop, OutMissing);
	for (FSoundStagePiece& P : Pieces)
	{
		Resolve(*this, P.SculpturePath, P.Sculpture, OutMissing);
		Resolve(*this, P.PlinthPath, P.Plinth, OutMissing);
	}
	return OutMissing.IsEmpty();
}

// ---- Accessors ---------------------------------------------------------------------------------------------------

bool USoundStageAsset::FindVolume(FName Name, FSoundStageVolume& OutVolume) const
{
	if (const FSoundStageVolume* V = Volumes.FindByPredicate([Name](const FSoundStageVolume& V) { return V.Name == Name; }))
	{
		OutVolume = *V;
		return true;
	}
	return false;
}

TArray<FSoundStageVolume> USoundStageAsset::GetVolumesOfKind(ESoundStageVolumeKind Kind) const
{
	return Volumes.FilterByPredicate([Kind](const FSoundStageVolume& V) { return V.Kind == Kind; });
}

void USoundStageAsset::SetVolume(const FSoundStageVolume& Volume)
{
	Modify();
	if (FSoundStageVolume* V = Volumes.FindByPredicate([&](const FSoundStageVolume& V) { return V.Name == Volume.Name; }))
	{
		*V = Volume;
	}
	else
	{
		Volumes.Add(Volume);
	}
	OnStageChanged.Broadcast(this);
}

bool USoundStageAsset::RemoveVolume(FName Name)
{
	Modify();
	const bool bRemoved = Volumes.RemoveAll([Name](const FSoundStageVolume& V) { return V.Name == Name; }) > 0;
	OnStageChanged.Broadcast(this);
	return bRemoved;
}

bool USoundStageAsset::FindPiece(FName Name, FSoundStagePiece& OutPiece) const
{
	if (const FSoundStagePiece* P = Pieces.FindByPredicate([Name](const FSoundStagePiece& P) { return P.Name == Name; }))
	{
		OutPiece = *P;
		return true;
	}
	return false;
}

void USoundStageAsset::SetPiece(const FSoundStagePiece& Piece)
{
	Modify();
	if (FSoundStagePiece* P = Pieces.FindByPredicate([&](const FSoundStagePiece& P) { return P.Name == Piece.Name; }))
	{
		*P = Piece;
	}
	else
	{
		Pieces.Add(Piece);
	}
	OnStageChanged.Broadcast(this);
}

bool USoundStageAsset::RemovePiece(FName Name)
{
	Modify();
	const bool bRemoved = Pieces.RemoveAll([Name](const FSoundStagePiece& P) { return P.Name == Name; }) > 0;
	OnStageChanged.Broadcast(this);
	return bRemoved;
}

FTransform USoundStageAsset::GetVolumeTransform(const FSoundStageVolume& Volume)
{
	// The package's yaw (+X toward +Z) is the stage's (+X toward +Y): Z maps to Y
	return FTransform(FRotator(Volume.Pitch, Volume.Yaw, 0.0f), PackageToStage(Volume.Center));
}

#if WITH_EDITOR
void USoundStageAsset::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	OnStageChanged.Broadcast(this);
}
#endif

// ---- The scene spec ----------------------------------------------------------------------------------------------

bool USoundStageAsset::ImportSpec(const FString& Json, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		OutError = TEXT("not a JSON object");
		return false;
	}
	int32 Version = 0;
	if (!Root->TryGetNumberField(TEXT("soundstage"), Version) || Version < 1 || Version > SpecVersion)
	{
		OutError = FString::Printf(TEXT("\"soundstage\" must be a spec version from 1 to %d"), SpecVersion);
		return false;
	}

	Modify();
	TArray<FString> Missing, Problems;
	BackdropPath = Root->GetStringField(TEXT("backdrop"));
	Backdrop = nullptr;
	StartEye = FVector::ZeroVector;
	StartLook = FVector(0, 0, -1);
	const TSharedPtr<FJsonObject>* Start = nullptr;
	if (Root->TryGetObjectField(TEXT("start"), Start))
	{
		ReadVector(*Start, TEXT("eye"), StartEye);
		ReadVector(*Start, TEXT("look"), StartLook);
	}
	DropShadowDirection = FVector(0.6, -0.6, 0.5);
	DropShadowOpacity = 0.5f;
	const TSharedPtr<FJsonObject>* Shadow = nullptr;
	if (Root->TryGetObjectField(TEXT("drop_shadow"), Shadow))
	{
		ReadVector(*Shadow, TEXT("direction"), DropShadowDirection);
		double Opacity;
		if ((*Shadow)->TryGetNumberField(TEXT("opacity"), Opacity)) { DropShadowOpacity = (float)FMath::Clamp(Opacity, 0.0, 1.0); }
	}
	ColourMatchStrength = 0.0f;
	ColourMatchRadius = 3.0f;
	const TSharedPtr<FJsonObject>* Match = nullptr;
	if (Root->TryGetObjectField(TEXT("colour_match"), Match))
	{
		double D;
		if ((*Match)->TryGetNumberField(TEXT("strength"), D)) { ColourMatchStrength = (float)FMath::Clamp(D, 0.0, 1.0); }
		if ((*Match)->TryGetNumberField(TEXT("radius"), D)) { ColourMatchRadius = (float)FMath::Max(D, 0.1); }
	}

	Volumes.Reset();
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (Root->TryGetArrayField(TEXT("volumes"), List))
	{
		for (const TSharedPtr<FJsonValue>& Value : *List)
		{
			const TSharedPtr<FJsonObject> O = Value->AsObject();
			if (!O.IsValid()) { continue; }
			FSoundStageVolume V;
			V.Name = FName(O->GetStringField(TEXT("name")));
			if (!ParseEnum(O->GetStringField(TEXT("kind")), KindNames, V.Kind)) { Problems.Add(V.Name.ToString() + TEXT(": unknown kind")); }
			FString Shape = TEXT("box");
			O->TryGetStringField(TEXT("shape"), Shape);
			if (!ParseEnum(Shape, ShapeNames, V.Shape)) { Problems.Add(V.Name.ToString() + TEXT(": unknown shape")); }
			if (!ReadVector(O, TEXT("center"), V.Center)) { Problems.Add(V.Name.ToString() + TEXT(": no center")); }
			if (!ReadVector(O, TEXT("size"), V.Size)) { Problems.Add(V.Name.ToString() + TEXT(": no size")); }
			V.Yaw = ReadHeading(O, TEXT("yaw"), 0.0f);
			V.Pitch = ReadHeading(O, TEXT("pitch"), 0.0f);
			Volumes.Add(V);
		}
	}

	Pieces.Reset();
	if (Root->TryGetArrayField(TEXT("pieces"), List))
	{
		for (const TSharedPtr<FJsonValue>& Value : *List)
		{
			const TSharedPtr<FJsonObject> O = Value->AsObject();
			if (!O.IsValid()) { continue; }
			FSoundStagePiece P;
			P.Name = FName(O->GetStringField(TEXT("name")));
			P.SculpturePath = O->GetStringField(TEXT("sculpture"));
			if (!ReadVector(O, TEXT("floor"), P.Floor)) { Problems.Add(P.Name.ToString() + TEXT(": no floor")); }
			P.Facing = ReadHeading(O, TEXT("facing"), 0.0f);
			double D;
			if (O->TryGetNumberField(TEXT("height"), D)) { P.Height = (float)D; }
			if (O->TryGetNumberField(TEXT("front_offset"), D)) { P.FrontYawOffset = (float)D; }
			if (O->TryGetNumberField(TEXT("plinth_emissive"), D)) { P.PlinthEmissive = (float)D; }
			O->TryGetStringField(TEXT("plinth"), P.PlinthPath);
			O->TryGetBoolField(TEXT("uplight"), P.bUplight);
			O->TryGetBoolField(TEXT("shadow"), P.bDropShadow);
			ReadVector(O, TEXT("shadow_direction"), P.DropShadowDirection);
			if (O->TryGetNumberField(TEXT("match"), D)) { P.ColourMatch = (float)FMath::Clamp(D, 0.0, 1.0); }
			if (O->TryGetNumberField(TEXT("stream_order"), D)) { P.StreamOrder = FMath::Max(0, (int32)D); }
			O->TryGetBoolField(TEXT("finest"), P.bAlwaysFinest);
			if (O->TryGetNumberField(TEXT("hide_below_eye"), D)) { P.HideBelowEye = (float)D; }
			Pieces.Add(P);
		}
	}

	ResolveAssets(Missing);
	OnStageChanged.Broadcast(this);
	for (const FString& Path : Missing) { Problems.Add(FString::Printf(TEXT("nothing at %s/%s"), *GetRoot(), *Path)); }
	OutError = FString::Join(Problems, TEXT("; "));
	return Problems.IsEmpty();
}

bool USoundStageAsset::ImportSpecFile(const FString& Filename, FString& OutError)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Filename))
	{
		OutError = TEXT("cannot read ") + Filename;
		return false;
	}
	SpecFile = Filename;
	return ImportSpec(Json, OutError);
}

FString USoundStageAsset::ExportSpec() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("soundstage"), SpecVersion);
	Root->SetStringField(TEXT("frame"), TEXT("package metres, Y up"));
	Root->SetStringField(TEXT("backdrop"), BackdropPath.IsEmpty() ? MakeRelativePath(Backdrop) : BackdropPath);
	TSharedRef<FJsonObject> Start = MakeShared<FJsonObject>();
	Start->SetField(TEXT("eye"), WriteVector(StartEye));
	Start->SetField(TEXT("look"), WriteVector(StartLook));
	Root->SetObjectField(TEXT("start"), Start);
	TSharedRef<FJsonObject> Shadow = MakeShared<FJsonObject>();
	Shadow->SetField(TEXT("direction"), WriteVector(DropShadowDirection));
	Shadow->SetNumberField(TEXT("opacity"), DropShadowOpacity);
	Root->SetObjectField(TEXT("drop_shadow"), Shadow);
	TSharedRef<FJsonObject> Match = MakeShared<FJsonObject>();
	Match->SetNumberField(TEXT("strength"), ColourMatchStrength);
	Match->SetNumberField(TEXT("radius"), ColourMatchRadius);
	Root->SetObjectField(TEXT("colour_match"), Match);

	TArray<TSharedPtr<FJsonValue>> VolumeList;
	for (const FSoundStageVolume& V : Volumes)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), V.Name.ToString());
		O->SetStringField(TEXT("kind"), KindNames[(int)V.Kind]);
		O->SetStringField(TEXT("shape"), ShapeNames[(int)V.Shape]);
		O->SetField(TEXT("center"), WriteVector(V.Center));
		O->SetField(TEXT("size"), WriteVector(V.Size));
		if (V.Yaw != 0.0f) { O->SetNumberField(TEXT("yaw"), V.Yaw); }
		if (V.Pitch != 0.0f) { O->SetNumberField(TEXT("pitch"), V.Pitch); }
		VolumeList.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("volumes"), VolumeList);

	TArray<TSharedPtr<FJsonValue>> PieceList;
	for (const FSoundStagePiece& P : Pieces)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), P.Name.ToString());
		O->SetStringField(TEXT("sculpture"), P.SculpturePath.IsEmpty() ? MakeRelativePath(P.Sculpture) : P.SculpturePath);
		O->SetField(TEXT("floor"), WriteVector(P.Floor));
		O->SetNumberField(TEXT("facing"), P.Facing);
		O->SetNumberField(TEXT("height"), P.Height);
		if (P.FrontYawOffset != 0.0f) { O->SetNumberField(TEXT("front_offset"), P.FrontYawOffset); }
		const FString Plinth = P.PlinthPath.IsEmpty() ? MakeRelativePath(P.Plinth) : P.PlinthPath;
		if (!Plinth.IsEmpty()) { O->SetStringField(TEXT("plinth"), Plinth); }
		O->SetNumberField(TEXT("plinth_emissive"), P.PlinthEmissive);
		O->SetBoolField(TEXT("uplight"), P.bUplight);
		if (P.bDropShadow) { O->SetBoolField(TEXT("shadow"), true); }
		if (!P.DropShadowDirection.IsNearlyZero()) { O->SetField(TEXT("shadow_direction"), WriteVector(P.DropShadowDirection)); }
		if (P.ColourMatch >= 0.0f) { O->SetNumberField(TEXT("match"), P.ColourMatch); }
		if (P.StreamOrder != 1) { O->SetNumberField(TEXT("stream_order"), P.StreamOrder); }
		if (P.bAlwaysFinest) { O->SetBoolField(TEXT("finest"), true); }
		if (P.HideBelowEye > -1.0e5f) { O->SetNumberField(TEXT("hide_below_eye"), P.HideBelowEye); }
		PieceList.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("pieces"), PieceList);

	FString Json;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
	return Json;
}

bool USoundStageAsset::ExportSpecFile(const FString& Filename, FString& OutError)
{
	if (!FFileHelper::SaveStringToFile(ExportSpec(), *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = TEXT("cannot write ") + Filename;
		return false;
	}
	SpecFile = Filename;
	return true;
}
