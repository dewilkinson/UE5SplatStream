// SplatAssetFactory.cpp

#include "SplatAssetFactory.h"

#include "Misc/FeedbackContext.h"
#include "SplatAsset.h"
#include "SplatPackageCore.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogSplatPakImport, Log, All);

UStaticMesh* USplatAssetFactory::BuildShadowMesh(USplatAsset* Asset, UObject* Outer, FName MeshName, EObjectFlags Flags)
{
	const FName InName = Asset->GetFName();
	std::vector<SplatCore::FShadowMeshLevel> Levels;
	FString Error;
	if (!Asset->ReadShadowMeshLevels(Levels, &Error))
	{
		if (!Error.IsEmpty())
		{
			UE_LOG(LogSplatPakImport, Warning, TEXT("%s: the shadow mesh could not be read: %s"), *InName.ToString(), *Error);
		}
		else
		{
			UE_LOG(LogSplatPakImport, Log, TEXT("%s has no shadow mesh (format %d): it casts no shadow. Rebake it in SplatPak to add one."), *InName.ToString(), Asset->FormatVersion);
		}
		return nullptr;
	}

	UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, MeshName, Flags);
	Mesh->SetNumSourceModels((int32)Levels.size());
	for (int32 Lod = 0; Lod < (int32)Levels.size(); Lod++)
	{
		const SplatCore::FShadowMeshLevel& L = Levels[Lod];
		FMeshDescription* Description = Mesh->CreateMeshDescription(Lod);
		FStaticMeshAttributes Attributes(*Description);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector4f> Colours = Attributes.GetVertexInstanceColors();
		Description->ReserveNewVertices((int32)L.Positions.size());
		Description->ReserveNewVertexInstances((int32)L.Indices.size());
		Description->ReserveNewTriangles((int32)L.Indices.size() / 3);
		const FPolygonGroupID Group = Description->CreatePolygonGroup();
		std::vector<FVertexID> Vertices(L.Positions.size());
		for (size_t i = 0; i < L.Positions.size(); i++)
		{
			Vertices[i] = Description->CreateVertex();
			const SplatCore::FVec3& P = L.Positions[i];
			Positions[Vertices[i]] = FVector3f(P.X * 100.0f, P.Z * 100.0f, P.Y * 100.0f); // Y-up metres to Z-up centimetres
		}
		for (size_t t = 0; t + 2 < L.Indices.size(); t += 3)
		{
			TArray<FVertexInstanceID, TFixedAllocator<3>> Corners;
			for (int k = 0; k < 3; k++)
			{
				const uint32 Index = L.Indices[t + k];
				const FVertexInstanceID Instance = Description->CreateVertexInstance(Vertices[Index]);
				const uint32 C = Index < L.Colours.size() ? L.Colours[Index] : 0xFF808080u;
				Colours[Instance] = FVector4f((C & 0xFF) / 255.0f, ((C >> 8) & 0xFF) / 255.0f, ((C >> 16) & 0xFF) / 255.0f, 1.0f);
				Corners.Add(Instance);
			}
			Description->CreateTriangle(Group, Corners);
		}
		Mesh->CommitMeshDescription(Lod);
		FStaticMeshSourceModel& Source = Mesh->GetSourceModel(Lod);
		Source.BuildSettings.bRecomputeNormals = true;
		Source.BuildSettings.bRecomputeTangents = true;
		Source.BuildSettings.bGenerateLightmapUVs = false;
		Source.BuildSettings.bRemoveDegenerates = true;
		Source.BuildSettings.DistanceFieldResolutionScale = 2.0f; // Small meshes otherwise get a coarse field
		Source.ScreenSize.Default = Lod == 0 ? 1.0f : Lod == 1 ? 0.3f : 0.1f;
	}
	Mesh->bAutoComputeLODScreenSize = false;
	Mesh->NaniteSettings.bEnabled = false;
	Mesh->CreateBodySetup();
	Mesh->GetBodySetup()->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	Mesh->Build(false);
	Mesh->PostEditChange();
	UE_LOG(LogSplatPakImport, Log, TEXT("%s: shadow mesh %s with %d LODs (%d triangles at LOD 0)"), *InName.ToString(), *MeshName.ToString(),
		(int32)Levels.size(), (int32)Levels[0].Indices.size() / 3);
	return Mesh;
}

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
	// The shadow mesh as its own asset beside this one (<Name>_ShadowMesh)
	{
		const FString MeshName = InName.ToString() + TEXT("_ShadowMesh");
		UPackage* MeshPackage = CreatePackage(*(FPackageName::GetLongPackagePath(InParent->GetOutermost()->GetName()) / MeshName));
		Asset->ShadowMesh = BuildShadowMesh(Asset, MeshPackage, FName(*MeshName), (Flags | RF_Public | RF_Standalone) & ~RF_Transient);
		if (Asset->ShadowMesh)
		{
			FAssetRegistryModule::AssetCreated(Asset->ShadowMesh);
			MeshPackage->MarkPackageDirty();
		}
	}
	return Asset;
}
