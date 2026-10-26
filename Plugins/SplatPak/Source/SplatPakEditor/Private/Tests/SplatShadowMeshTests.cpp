// SplatShadowMeshTests.cpp
// Unreal automation tests for the shadow mesh (3DGSStream docs/requirements/gstx-shadow-mesh.md, criterion 9): a splat
// model whose package carries a shadow mesh blocks line traces at its visible surface, and lets them through with
// Collision off. Run with Scripts/Test.ps1 -Filter SplatPak.ShadowMesh.

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshCompiler.h"
#include "DistanceFieldAtlas.h"
#include "StaticMeshResources.h"
#include "Engine/World.h"
#include "SplatActor.h"
#include "SplatAsset.h"
#include "SplatAssetFactory.h"
#include "SplatComponent.h"
#include "SplatPackageCore.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplatShadowMeshTraceTest, "SplatPak.ShadowMesh.Trace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSplatShadowMeshTraceTest::RunTest(const FString& Parameters)
{
	FString Repo = FPlatformMisc::GetEnvironmentVariable(TEXT("SPLATPAK_REPO"));
	if (Repo.IsEmpty()) Repo = TEXT("C:/github/3DGSStream");
	const FString File = FPaths::Combine(Repo, TEXT("assets/dwarves/dwarves.gstx"));
	TArray64<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *File))
	{
		AddError(FString::Printf(TEXT("Could not read %s"), *File));
		return false;
	}

	USplatAsset* Asset = NewObject<USplatAsset>(GetTransientPackage(), TEXT("TraceTestAsset"), RF_Transient);
	FString Error;
	if (!Asset->SetPackageData(TArrayView<const uint8>(Bytes.GetData(), (int32)Bytes.Num()), Error))
	{
		AddError(TEXT("SetPackageData failed: ") + Error);
		return false;
	}
	Asset->ShadowMesh = USplatAssetFactory::BuildShadowMesh(Asset, GetTransientPackage(), TEXT("TraceTestAsset_ShadowMesh"), RF_Transient);
	if (!Asset->ShadowMesh)
	{
		AddError(TEXT("The package has no shadow mesh (rebake it with a version 12 build)."));
		return false;
	}

	FStaticMeshCompilingManager::Get().FinishAllCompilation(); // The mesh builds asynchronously; its collision with it
	if (GDistanceFieldAsyncQueue) GDistanceFieldAsyncQueue->BlockUntilAllBuildsComplete();
	// Criterion 10: the mesh carries a distance field (Lumen and distance-field shadows read it)
	{
		const FStaticMeshRenderData* RenderData = Asset->ShadowMesh->GetRenderData();
		const FDistanceFieldVolumeData* Field = RenderData && RenderData->LODResources.Num() > 0 ? RenderData->LODResources[0].DistanceFieldData : nullptr;
		TestTrue(TEXT("the shadow mesh has a mesh distance field"), Field != nullptr && Field->LocalSpaceMeshBounds.IsValid);
	}
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		AddError(TEXT("No editor world to place the model in."));
		return false;
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	ASplatActor* Actor = World->SpawnActor<ASplatActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Actor->SplatComponent->SetSplatAsset(Asset);
	UStaticMeshComponent* MeshComponent = Actor->SplatComponent->GetShadowMeshComponent();
	TestNotNull(TEXT("hidden shadow mesh component"), MeshComponent);
	if (MeshComponent)
	{
		TestFalse(TEXT("the shadow mesh is not drawn"), MeshComponent->IsVisible());
		TestTrue(TEXT("the shadow mesh casts shadows while hidden"), MeshComponent->CastShadow && MeshComponent->bCastHiddenShadow);
		TestTrue(TEXT("the shadow mesh feeds distance-field lighting and Lumen while hidden"),
			MeshComponent->bAffectDistanceFieldLighting && MeshComponent->bAffectIndirectLightingWhileHidden);
	}

	// Trace from outside the model towards its centre along a few axes, and compare each hit with the visible surface:
	// the first level 0 splat within a narrow cylinder around the ray
	FSplatLevelPtr Level0 = Asset->GetDecodedLevel(0, &Error);
	const FMatrix ModelToWorld = Actor->SplatComponent->GetModelToLocal() * Actor->SplatComponent->GetComponentTransform().ToMatrixWithScale();
	const FBox Bounds = Asset->GetModelBounds().TransformBy(ModelToWorld);
	const double Diagonal = (Bounds.Max - Bounds.Min).Size();
	const FVector Centre = Bounds.GetCenter();
	std::vector<FVector> Splats;
	for (uint32 i = 0; Level0.IsValid() && i < Level0->SplatCount; i++)
	{
		const SplatCore::FVec3 P = SplatCore::DecodePosition(*Level0, i);
		Splats.push_back(ModelToWorld.TransformPosition(FVector(P.X, P.Y, P.Z)));
	}
	const FVector Directions[] = { FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0), FVector(0, 0, -1) };
	int32 Checked = 0;
	for (const FVector& Dir : Directions)
	{
		const FVector Start = Centre - Dir * Diagonal, End = Centre + Dir * Diagonal;
		// Splats near the ray (within 0.5% of the diagonal), by distance along it
		std::vector<double> Along;
		for (const FVector& S : Splats)
		{
			const double T = FVector::DotProduct(S - Start, Dir);
			if (T > 0 && (S - (Start + Dir * T)).Size() < 0.005 * Diagonal) Along.push_back(T);
		}
		if (Along.size() < 20) continue; // The ray misses the model
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility);
		const bool bOnModel = bHit && Hit.GetComponent() == MeshComponent;
		TestTrue(FString::Printf(TEXT("trace along (%g %g %g) hits the shadow mesh"), Dir.X, Dir.Y, Dir.Z), bOnModel);
		if (bOnModel)
		{
			// On the visible surface: a splat lies within 2% of the diagonal of the hit point, and the ray did not pass through
			// the surface first (at most a few haze splats before the hit)
			double Nearest = TNumericLimits<double>::Max();
			for (const FVector& S : Splats) Nearest = FMath::Min(Nearest, (S - Hit.ImpactPoint).Size());
			int32 Before = 0;
			for (double T : Along) Before += T < Hit.Distance - 0.02 * Diagonal ? 1 : 0;
			AddInfo(FString::Printf(TEXT("trace along (%g %g %g): hit at %.1f, nearest splat %.2f%% of the diagonal away, %d of %d splats near the ray lie before the hit"),
				Dir.X, Dir.Y, Dir.Z, Hit.Distance, 100.0 * Nearest / Diagonal, Before, (int)Along.size()));
			TestTrue(FString::Printf(TEXT("trace along (%g %g %g) hits within 2%% of the diagonal of a splat (%.2f%%)"), Dir.X, Dir.Y, Dir.Z, 100.0 * Nearest / Diagonal),
				Nearest <= 0.02 * Diagonal);
			TestTrue(FString::Printf(TEXT("trace along (%g %g %g) does not pass through the visible surface (%d splats before the hit)"), Dir.X, Dir.Y, Dir.Z, Before),
				Before <= 9);
		}
		Checked++;
	}
	TestTrue(TEXT("at least three traces reached the model"), Checked >= 3);

	// Affect Lumen off: the mesh no longer feeds distance-field lighting or Lumen
	Actor->SplatComponent->bShadowMeshAffectLumen = false;
	Actor->SplatComponent->SetSplatAsset(nullptr);
	Actor->SplatComponent->SetSplatAsset(Asset);
	if (UStaticMeshComponent* M = Actor->SplatComponent->GetShadowMeshComponent())
	{
		TestFalse(TEXT("with Affect Lumen off the mesh stays out of distance-field lighting and Lumen"), M->bAffectDistanceFieldLighting || M->bAffectIndirectLightingWhileHidden);
	}
	Actor->SplatComponent->bShadowMeshAffectLumen = true;

	// Collision off: traces pass through
	Actor->SplatComponent->bShadowMeshCollision = false;
	Actor->SplatComponent->SetSplatAsset(nullptr);
	Actor->SplatComponent->SetSplatAsset(Asset);
	{
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, Centre - FVector(1, 0, 0) * Diagonal, Centre + FVector(1, 0, 0) * Diagonal, ECC_Visibility);
		TestFalse(TEXT("with Collision off the trace passes through"), bHit && Hit.GetComponent() == Actor->SplatComponent->GetShadowMeshComponent());
	}
	World->DestroyActor(Actor);
	return true;
}

#endif
