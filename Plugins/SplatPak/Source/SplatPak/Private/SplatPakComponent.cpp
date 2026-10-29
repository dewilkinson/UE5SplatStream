// SplatPakComponent.cpp

#include "SplatPakComponent.h"

#include "Async/Async.h"
#include "SplatPakAsset.h"
#include "SplatPakSceneProxy.h"
#include "SplatPakStreamLevels.h"
#include "SplatPakVeil.h"
#include "SplatPakPackageCore.h"
#include "ConvexVolume.h"
#include "RenderingThread.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#endif
#include <cstring>

DEFINE_LOG_CATEGORY_STATIC(LogSplatPak, Log, All);

static_assert(sizeof(Splat::MeshletChunkGPU) == sizeof(SplatCore::FMeshlet), "The runtime's render list is uploaded as the shaders' meshlets");

USplatPakComponent::USplatPakComponent()
{
	CastShadow = false; // Splat shadow casting is a later, separate depth pass per light
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	bUseAsOccluder = false;
	// Streaming follows the camera every frame, in the editor too, after the camera has moved
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	bTickInEditor = true;
}

void USplatPakComponent::SetSplatPakAsset(USplatPakAsset* NewAsset)
{
	if (SplatPakAsset != NewAsset)
	{
		StopStream();
		SplatPakAsset = NewAsset;
		ArmLoadingVeil();
		UpdateBounds();
		MarkRenderStateDirty();
		UpdateShadowMeshComponent();
	}
}

void USplatPakComponent::OnRegister()
{
	Super::OnRegister();
	UpdateShadowMeshComponent();
	ArmLoadingVeil(); // A scan appearing: veiled until its view has loaded
}

void USplatPakComponent::OnUnregister()
{
	PublishVeil(0.0f);
	bVeilArmed = false;
	if (ShadowMeshComponent)
	{
		ShadowMeshComponent->DestroyComponent();
		ShadowMeshComponent = nullptr;
	}
	Super::OnUnregister();
}

// The shadow mesh as a hidden child: never drawn, but casting shadows (bCastHiddenShadow), feeding Lumen while hidden
// and blocking traces, each as the three Shadow Mesh settings ask. The mesh is built in the default import frame, so the
// child is scaled by any change to UnitsToCentimeters; a component that turns off the Y-up conversion gets none.
void USplatPakComponent::UpdateShadowMeshComponent()
{
	UStaticMesh* Mesh = SplatPakAsset ? SplatPakAsset->ShadowMesh.Get() : nullptr;
	const bool bWanted = Mesh && IsRegistered() && GetWorld() && bConvertYUpToZUp && (bShadowMeshCastShadow || bShadowMeshAffectLumen || bShadowMeshCollision);
	if (!bWanted)
	{
		if (ShadowMeshComponent) { ShadowMeshComponent->DestroyComponent(); ShadowMeshComponent = nullptr; }
		return;
	}
	if (!ShadowMeshComponent)
	{
		ShadowMeshComponent = NewObject<UStaticMeshComponent>(GetOwner() ? (UObject*)GetOwner() : (UObject*)this, NAME_None, RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
		ShadowMeshComponent->SetupAttachment(this);
		ShadowMeshComponent->bIsEditorOnly = false;
		ShadowMeshComponent->RegisterComponent();
	}
	ShadowMeshComponent->SetStaticMesh(Mesh);
	ShadowMeshComponent->SetRelativeTransform(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(UnitsToCentimeters / 100.0f)));
	ShadowMeshComponent->SetVisibility(false);
	ShadowMeshComponent->SetHiddenInGame(true);
	ShadowMeshComponent->SetCastShadow(bShadowMeshCastShadow);
	ShadowMeshComponent->bCastHiddenShadow = bShadowMeshCastShadow;
	ShadowMeshComponent->bAffectDistanceFieldLighting = bShadowMeshAffectLumen;
	ShadowMeshComponent->bAffectDynamicIndirectLighting = bShadowMeshAffectLumen;
	ShadowMeshComponent->bAffectIndirectLightingWhileHidden = bShadowMeshAffectLumen;
	ShadowMeshComponent->SetCollisionProfileName(bShadowMeshCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	ShadowMeshComponent->MarkRenderStateDirty();
}

bool USplatPakComponent::GetTrainingCamera(int32 Index, FTransform& WorldTransform, float& FovYDegrees) const
{
	if (!SplatPakAsset || !SplatPakAsset->TrainingCameras.IsValidIndex(Index)) return false;
	const FSplatPakTrainingCamera& C = SplatPakAsset->TrainingCameras[Index];
	const FMatrix ModelToWorld = GetModelToLocal() * GetComponentTransform().ToMatrixWithScale();
	const FVector Eye = ModelToWorld.TransformPosition(FVector(C.Eye));
	const FVector Forward = ModelToWorld.TransformVector(FVector(C.Forward)).GetSafeNormal();
	const FVector Up = ModelToWorld.TransformVector(FVector(C.Up)).GetSafeNormal();
	WorldTransform = FTransform(FRotationMatrix::MakeFromXZ(Forward, Up).Rotator(), Eye);
	FovYDegrees = C.FovYDegrees;
	return true;
}

FMatrix USplatPakComponent::GetModelToLocal() const
{
	const double S = UnitsToCentimeters;
	if (bConvertYUpToZUp)
	{
		// Rows are the images of the model axes (Unreal's row-vector convention): X -> X, Y -> Z, Z -> Y.
		return FMatrix(FPlane(S, 0, 0, 0), FPlane(0, 0, S, 0), FPlane(0, S, 0, 0), FPlane(0, 0, 0, 1));
	}
	return FScaleMatrix(FVector(S));
}

void USplatPakComponent::SetEmissiveIntensity(float NewIntensity)
{
	EmissiveIntensity = FMath::Max(NewIntensity, 0.0f);
	if (FSplatPakSceneProxy* Proxy = static_cast<FSplatPakSceneProxy*>(SceneProxy))
	{
		// Only a shader constant changes: hand it to the render thread instead of rebuilding the proxy.
		ENQUEUE_RENDER_COMMAND(SetSplatEmissiveIntensity)([Proxy, Intensity = EmissiveIntensity](FRHICommandListImmediate&)
		{
			Proxy->SetEmissiveIntensity_RenderThread(Intensity);
		});
	}
}

void USplatPakComponent::SetLighting(bool bNewLit, float NewLitBlend, float NewAlbedoScale, float NewRoughness, float NewSpecular)
{
	bLit = bNewLit;
	LitBlend = FMath::Clamp(NewLitBlend, 0.0f, 1.0f);
	AlbedoScale = FMath::Max(NewAlbedoScale, 0.0f);
	LitRoughness = FMath::Clamp(NewRoughness, 0.0f, 1.0f);
	LitSpecular = FMath::Clamp(NewSpecular, 0.0f, 1.0f);
	PushLightingSettings();
}

void USplatPakComponent::PushLightingSettings() const
{
	if (FSplatPakSceneProxy* Proxy = static_cast<FSplatPakSceneProxy*>(SceneProxy))
	{
		ENQUEUE_RENDER_COMMAND(SetSplatLightingSettings)([Proxy, bOn = bLit, Blend = LitBlend, Roughness = LitRoughness, Specular = LitSpecular, Albedo = AlbedoScale,
			Smoothing = NormalSmoothing](FRHICommandListImmediate&)
		{
			Proxy->SetLightingSettings_RenderThread(bOn, Blend, Roughness, Specular, Albedo, Smoothing);
		});
	}
}

FPrimitiveSceneProxy* USplatPakComponent::CreateSceneProxy()
{
	if (!SplatPakAsset)
	{
		return nullptr;
	}

	FString Error;
	FSplatPakLevelPtr Level = SplatPakAsset->GetDecodedLevel(ResidentLevel, &Error);
	if (!Level.IsValid())
	{
		UE_LOG(LogSplatPak, Warning, TEXT("%s: could not decode %s: %s"), *GetPathName(), *SplatPakAsset->GetName(), *Error);
		return nullptr;
	}

	FSplatPakDrawSettings Settings;
	Settings.SplatScale = SplatScale;
	Settings.Coverage = Coverage;
	Settings.Orient = (Orientation == ESplatPakOrientation::Billboard) ? ESplatPakOrient::Billboard : ESplatPakOrient::Disc;
	Settings.EmissiveIntensity = EmissiveIntensity;
	Settings.bDitheredTransitions = bDitheredTransitions;
	Settings.bRefinementVisualizer = bRefinementVisualizer;
	Settings.GlowIntensity = GlowIntensity;
	Settings.GlowHue = GlowHue;
	Settings.bShadowMeshFill = bShadowMeshFill;
	Settings.bOccluderDepth = bOccluderDepth;
	Settings.bGaussians = bGaussians;
	Settings.bEnvironment = SplatPakAsset && SplatPakAsset->bEnvironmentScene;
	Settings.ColourGain = FVector3f(ColourGain.R, ColourGain.G, ColourGain.B);
	Settings.ColourOffset = FVector3f(ColourOffset.R, ColourOffset.G, ColourOffset.B);
	Settings.bDropShadow = bDropShadow && DropShadowDirection.Z < -0.05;
	Settings.DropShadowDirection = FVector3f(DropShadowDirection.GetSafeNormal());
	Settings.DropShadowOpacity = DropShadowOpacity;
	Settings.DropShadowFloorZ = DropShadowFloorZ;
	Settings.bDropShadowBox = DropShadowBox.IsValid != 0;
	Settings.DropShadowBoxMin = FVector3f(DropShadowBox.Min);
	Settings.DropShadowBoxMax = FVector3f(DropShadowBox.Max);
	Settings.bLit = bLit;
	Settings.LitBlend = LitBlend;
	Settings.LitRoughness = LitRoughness;
	Settings.LitSpecular = LitSpecular;
	Settings.AlbedoScale = AlbedoScale;
	Settings.NormalSmoothing = NormalSmoothing;
	// A new proxy takes the stream's records now; its render list arrives with the next tick
	return new FSplatPakSceneProxy(this, MoveTemp(Level), Settings, bStreaming ? StreamSource : nullptr);
}

FBoxSphereBounds USplatPakComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!SplatPakAsset)
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0);
	}
	return FBoxSphereBounds(SplatPakAsset->GetModelBounds().TransformBy(GetModelToLocal() * LocalToWorld.ToMatrixWithScale()));
}

// ---- Streaming ----------------------------------------------------------------------------------------------------

void USplatPakComponent::StartStream()
{
	// The runtime is set up on a worker thread (cutting every level into blocks and measuring their spacing takes a few
	// seconds on a large scan); it is shared with the game thread only once it is ready.
	TSharedPtr<const FSplatPakStreamLevels, ESPMode::ThreadSafe> Levels = StreamLevels;
	TSharedPtr<Splat::StreamRuntime> NewRuntime = MakeShared<Splat::StreamRuntime>();
	TSharedPtr<FSplatPakStreamSource, ESPMode::ThreadSafe> Source = MakeShared<FSplatPakStreamSource, ESPMode::ThreadSafe>();
	RuntimeInit = Async(EAsyncExecution::ThreadPool, [Levels, NewRuntime, Source]() -> bool
	{
		Splat::StreamRuntime& R = *NewRuntime;
		R.m_levels = &Levels->Levels;
		R.m_waveletLevels = (int)Levels->Levels.size();
		R.m_aabbMin = Levels->BoundsMin;
		R.m_aabbMax = Levels->BoundsMax;
		R.m_center = Splat::XMFLOAT3(0.5f * (R.m_aabbMin.x + R.m_aabbMax.x), 0.5f * (R.m_aabbMin.y + R.m_aabbMax.y), 0.5f * (R.m_aabbMin.z + R.m_aabbMax.z));
		R.m_extents = Splat::XMFLOAT3(R.m_aabbMax.x - R.m_aabbMin.x, R.m_aabbMax.y - R.m_aabbMin.y, R.m_aabbMax.z - R.m_aabbMin.z);
		R.m_lodSortFrameMin = R.m_aabbMin;
		R.m_lodSortFrameMax = R.m_aabbMax;
		R.m_splatMode = true;
		R.m_splatParams.aabbMin = R.m_aabbMin;
		R.m_splatParams.aabbMax = R.m_aabbMax;
		R.m_splatParams.scaleLog2Min = Levels->ScaleLog2Min;
		R.m_splatParams.scaleLog2Max = Levels->ScaleLog2Max;
		R.m_splatParams.shDegree = 0; // The discs draw the stored colour; the harmonics count only in the download
		R.m_bytesPerRenderPoint = sizeof(Splat::PackedSplatGPU) + Levels->SHRecordBytes;
		R.settings.splatDrawAsDiscs = true; // Cross-fades as dither weights (blendWeight), which the disc pass draws
		R.InitStreamingSimulation();
		R.ClearResidentStream(); // Starts from the two coarsest levels, so the model streams in

		Source->RecordWords.resize(R.m_unifiedSplats.size() * (sizeof(Splat::PackedSplatGPU) / sizeof(uint32)));
		if (!R.m_unifiedSplats.empty()) memcpy(Source->RecordWords.data(), R.m_unifiedSplats.data(), R.m_unifiedSplats.size() * sizeof(Splat::PackedSplatGPU));
		for (size_t L = 0; L < R.m_lodSpacing.size() && L < 8; L++) Source->LevelSpacing[L] = R.m_lodSpacing[L];
		return !R.m_allStreamChunkPtrs.empty();
	});
	PendingRuntime = NewRuntime;
	PendingSource = Source;
	UE_LOG(LogSplatPak, Log, TEXT("%s: preparing to stream %s (%d levels, prepared in %.1f s)"), *GetPathName(), *SplatPakAsset->GetName(), (int32)StreamLevels->Levels.size(), StreamLevels->Seconds);
}

void USplatPakComponent::StopStream()
{
	if (RuntimeInit.IsValid()) RuntimeInit.Wait(); // The worker holds the pending runtime
	RuntimeInit = TFuture<bool>();
	PendingRuntime.Reset();
	PendingSource.Reset();
	Runtime.Reset();
	StreamLevels.Reset();
	bStreamFailed = false;
	ViewLevel = 0;
	if (StreamSource.IsValid())
	{
		StreamSource.Reset();
		MarkRenderStateDirty(); // Back to the resident level
	}
}

bool USplatPakComponent::IsStreamReady() const
{
	return Runtime.IsValid();
}

void USplatPakComponent::Evict()
{
	if (Runtime.IsValid())
	{
		Runtime->ClearResidentStream();
	}
	ArmLoadingVeil();
}

void USplatPakComponent::Restream()
{
	if (Runtime.IsValid())
	{
		Runtime->ResetStreamingSimulation();
		Runtime->ClearResidentStream();
		Runtime->m_redeliveredBlocks = 0;
		Runtime->m_extraDeliveries = 0;
	}
	ArmLoadingVeil();
}

void USplatPakComponent::SetDropShadow(bool bEnabled, FVector Direction, float Opacity, float FloorZ, FBox Box)
{
	const bool bChanged = bDropShadow != bEnabled || !DropShadowDirection.Equals(Direction) || DropShadowOpacity != Opacity
		|| DropShadowFloorZ != FloorZ || !(DropShadowBox == Box);
	bDropShadow = bEnabled;
	DropShadowDirection = Direction;
	DropShadowOpacity = Opacity;
	DropShadowFloorZ = FloorZ;
	DropShadowBox = Box;
	if (bChanged) MarkRenderStateDirty(); // The settings travel with a new proxy
}

// ---- Colour match -------------------------------------------------------------------------------------------------

namespace
{
	// A package's shadow mesh (finest LOD) as colour samples: model-space positions and linear colours, read once per
	// asset (a pedestal's construction script runs on every edit)
	struct FColourSamples
	{
		TArray<FVector3f> Positions;
		TArray<FVector3f> Colours;
	};

	TSharedPtr<const FColourSamples> GetColourSamples(USplatPakAsset* Asset)
	{
		static TMap<TWeakObjectPtr<USplatPakAsset>, TSharedPtr<const FColourSamples>> Cache;
		if (!Asset) return nullptr;
		if (const TSharedPtr<const FColourSamples>* Found = Cache.Find(Asset)) return *Found;
		std::vector<SplatCore::FShadowMeshLevel> Levels;
		TSharedPtr<FColourSamples> Samples = MakeShared<FColourSamples>();
		if (Asset->ReadShadowMeshLevels(Levels) && !Levels.empty())
		{
			const SplatCore::FShadowMeshLevel& L = Levels[0];
			const int32 N = (int32)FMath::Min(L.Positions.size(), L.Colours.size());
			Samples->Positions.SetNumUninitialized(N);
			Samples->Colours.SetNumUninitialized(N);
			for (int32 i = 0; i < N; i++)
			{
				Samples->Positions[i] = FVector3f(L.Positions[i].X, L.Positions[i].Y, L.Positions[i].Z);
				const uint32 C = L.Colours[i]; // 0xAABBGGRR, sRGB
				const FLinearColor Lin = FLinearColor(FColor((uint8)(C & 0xFF), (uint8)((C >> 8) & 0xFF), (uint8)((C >> 16) & 0xFF)));
				Samples->Colours[i] = FVector3f(Lin.R, Lin.G, Lin.B);
			}
		}
		Cache.Add(Asset, Samples);
		return Samples;
	}

	// Mean and standard deviation per channel
	void ColourStats(const TArray<FVector3f>& Colours, FVector3f& OutMean, FVector3f& OutStd)
	{
		FVector3d Sum(0), Sum2(0);
		for (const FVector3f& C : Colours) { Sum += FVector3d(C); Sum2 += FVector3d(C) * FVector3d(C); }
		const double N = FMath::Max(1, Colours.Num());
		const FVector3d Mean = Sum / N;
		const FVector3d Var = Sum2 / N - Mean * Mean;
		OutMean = FVector3f(Mean);
		OutStd = FVector3f((float)FMath::Sqrt(FMath::Max(Var.X, 1e-8)), (float)FMath::Sqrt(FMath::Max(Var.Y, 1e-8)), (float)FMath::Sqrt(FMath::Max(Var.Z, 1e-8)));
	}
}

bool USplatPakComponent::MatchColours(USplatPakComponent* Scene, float Strength, float RadiusCm)
{
	const TSharedPtr<const FColourSamples> Own = GetColourSamples(SplatPakAsset);
	const TSharedPtr<const FColourSamples> Room = Scene ? GetColourSamples(Scene->SplatPakAsset) : nullptr;
	if (!Own || !Room || Own->Colours.Num() < 16 || Room->Colours.Num() < 16) return false;

	// The room around this object: the scene's samples within the radius, widened until there are enough
	const FMatrix SceneModelToWorld = Scene->GetModelToLocal() * Scene->GetComponentTransform().ToMatrixWithScale();
	const FVector Centre = Bounds.Origin;
	TArray<FVector3f> Near;
	for (float Radius = FMath::Max(RadiusCm, 10.0f); Near.Num() < 500 && Radius < 5000.0f; Radius *= 2.0f)
	{
		Near.Reset();
		const double R2 = (double)Radius * Radius;
		for (int32 i = 0; i < Room->Positions.Num(); i++)
		{
			if (FVector::DistSquared(SceneModelToWorld.TransformPosition(FVector(Room->Positions[i])), Centre) <= R2) Near.Add(Room->Colours[i]);
		}
	}
	if (Near.Num() < 16) return false;

	FVector3f OwnMean, OwnStd, RoomMean, RoomStd;
	ColourStats(Own->Colours, OwnMean, OwnStd);
	ColourStats(Near, RoomMean, RoomStd);
	MatchedRoomColour = FLinearColor(RoomMean.X, RoomMean.Y, RoomMean.Z, 1.0f);
	// A gain per channel, RoomMean / OwnMean (a white balance and exposure shift, held to 0.5 .. 2): the object takes the
	// room's light and colour cast but keeps its own contrast. Moving its spread to the room's as well (a full colour
	// transfer) flattened a glossy sculpture's highlights to the matte stone around it.
	const float S = FMath::Clamp(Strength, 0.0f, 1.0f);
	FVector3f Gain, Offset = FVector3f::ZeroVector;
	for (int32 c = 0; c < 3; c++)
	{
		const float A = FMath::Clamp(RoomMean[c] / FMath::Max(OwnMean[c], 1e-4f), 0.5f, 2.0f);
		Gain[c] = FMath::Lerp(1.0f, A, S);
	}
	const FLinearColor NewGain(Gain.X, Gain.Y, Gain.Z, 1.0f), NewOffset(Offset.X, Offset.Y, Offset.Z, 0.0f);
	if (!NewGain.Equals(ColourGain, 1e-4f) || !NewOffset.Equals(ColourOffset, 1e-4f))
	{
		ColourGain = NewGain;
		ColourOffset = NewOffset;
		MarkRenderStateDirty(); // The grade travels with a new proxy
	}
	return true;
}

void USplatPakComponent::ClearColourMatch()
{
	if (!ColourGain.Equals(FLinearColor::White) || !ColourOffset.Equals(FLinearColor(0, 0, 0, 0)))
	{
		ColourGain = FLinearColor::White;
		ColourOffset = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		MarkRenderStateDirty();
	}
}

float USplatPakComponent::ComputeViewFill(int32& OutInView, int32 Slack) const
{
	OutInView = 0;
	if (!Runtime.IsValid()) return 0.0f;
	FConvexVolume Frustum;
	GetViewFrustumBounds(Frustum, LastModelToClip, false);
	const Splat::XMFLOAT3& Eye = Runtime->camera.eye;
	const size_t Count = FMath::Min<size_t>(Runtime->FrameChunkCount(), Runtime->m_rendererSourceChunks.size());
	int32 Have = 0;
	for (size_t i = 0; i < Count; i++)
	{
		const Splat::StreamChunk* C = Runtime->m_rendererSourceChunks[i];
		if (!C || !Frustum.IntersectSphere(FVector(C->center.x, C->center.y, C->center.z), C->radius)) continue;
		OutInView++;
		Have += C->lodLevel <= Runtime->BlockTargetLOD(*C, Eye) + Slack ? 1 : 0;
	}
	return OutInView > 0 ? (float)Have / (float)OutInView : 1.0f;
}

bool USplatPakComponent::IsViewLoaded(float Threshold, bool bFinest) const
{
	if (!Runtime.IsValid() || bStreamHold || !Runtime->m_frameProduced) return false;
	int32 InView = 0;
	const float Fill = ComputeViewFill(InView, bFinest ? 0 : 1);
	if (InView > 0) return Fill >= Threshold;
	// Nothing drawn in view: loaded only if the scan is really out of view, not just not drawing yet
	const FMatrix ModelToWorld = GetModelToLocal() * GetComponentTransform().ToMatrixWithScale();
	FConvexVolume Frustum;
	GetViewFrustumBounds(Frustum, ModelToWorld.Inverse() * LastModelToClip, false);
	return !Frustum.IntersectSphere(Bounds.Origin, Bounds.SphereRadius);
}

void USplatPakComponent::SetStreamHold(bool bHold)
{
	bStreamHold = bHold;
}

void USplatPakComponent::ArmLoadingVeil()
{
	bVeilArmed = true;
	VeilSeconds = 0.0f;
	VeilReadySeconds = 0.0f;
	VeilFill = 0.0f;
	VeilFillTimer = 0.0f;
}

void USplatPakComponent::GetLoadingVeil(float& ViewFill, float& Strength) const
{
	ViewFill = VeilFill;
	Strength = VeilStrength;
}

void USplatPakComponent::PublishVeil(float Strength)
{
	const FSceneInterface* Scene = GetWorld() ? GetWorld()->Scene : nullptr;
	if (VeilScene && VeilScene != Scene) SplatPakVeil::SetStrength(VeilScene, this, 0.0f);
	SplatPakVeil::SetStrength(Scene, this, Strength);
	VeilScene = Strength > 0.0f ? Scene : nullptr;
}

// The loading veil: full while the stream initialises; then, four times a second, the share of the view's drawn blocks
// (the render list's, inside the view's frustum) drawn at their target level or the next coarser one. The blur eases
// out from 60% to 95%, where the veil disarms. A view that draws nothing a second after the stream started counts as in.
void USplatPakComponent::UpdateLoadingVeil(float DeltaTime)
{
	const UWorld* World = GetWorld();
	const bool bActive = bVeilArmed && bLoadingVeil && bStreaming && SplatPakAsset && !bStreamFailed && World && World->IsGameWorld()
		&& IsVisible() && VeilSeconds < VeilTimeout;
	float Target = 0.0f;
	if (bActive)
	{
		VeilSeconds += DeltaTime;
		Target = 1.0f;
		if (Runtime.IsValid())
		{
			VeilReadySeconds += DeltaTime;
			VeilFillTimer -= DeltaTime;
			if (VeilFillTimer <= 0.0f)
			{
				VeilFillTimer = 0.25f;
				int32 Wanted = 0;
				const float Fill = ComputeViewFill(Wanted, 1);
				VeilFill = Wanted > 0 ? Fill : (VeilReadySeconds > 1.0f ? 1.0f : 0.0f);
				if (VeilFill >= 0.95f) bVeilArmed = false; // In: no veil until the next arm
			}
			const float T = FMath::Clamp((VeilFill - 0.60f) / 0.35f, 0.0f, 1.0f);
			Target = bVeilArmed ? 1.0f - T * T * (3.0f - 2.0f * T) : 0.0f;
		}
	}
	else if (bVeilArmed && (VeilSeconds >= VeilTimeout || !bLoadingVeil || !bStreaming))
	{
		bVeilArmed = false;
	}
	// Up at once (a new scan veils straight away), down over about half a second
	VeilStrength = Target >= VeilStrength ? Target : FMath::FInterpTo(VeilStrength, Target, DeltaTime, 6.0f);
	if (VeilStrength < 0.01f) VeilStrength = 0.0f;
	if (VeilStrength > 0.0f || VeilScene) PublishVeil(VeilStrength);
}

TArray<FSplatPakLevelResidency> USplatPakComponent::GetLevelResidency() const
{
	TArray<FSplatPakLevelResidency> Out;
	if (!Runtime.IsValid()) return Out;
	for (int32 L = 0; L < (int32)Runtime->m_lodStreamChunks.size(); L++)
	{
		FSplatPakLevelResidency& R = Out.AddDefaulted_GetRef();
		R.Level = L;
		R.TotalBlocks = (int32)Runtime->m_lodStreamChunks[L].size();
		for (const Splat::StreamChunk& C : Runtime->m_lodStreamChunks[L]) R.ResidentBlocks += C.isResident ? 1 : 0;
		const size_t Total = L < (int32)Runtime->m_lodTotalSplats.size() ? Runtime->m_lodTotalSplats[L] : 0;
		const size_t Resident = L < (int32)Runtime->m_lodResidentSplats.size() ? Runtime->m_lodResidentSplats[L] : 0;
		R.ResidentFraction = Total > 0 ? (float)((double)Resident / (double)Total) : 0.0f;
	}
	return Out;
}

void USplatPakComponent::GetStreamProgress(float& DeliveredMB, float& TotalMB, float& Fraction) const
{
	DeliveredMB = TotalMB = Fraction = 0.0f;
	if (!Runtime.IsValid()) return;
	DeliveredMB = Runtime->m_simulatedBytesDelivered / (1024.0f * 1024.0f);
	TotalMB = Runtime->m_totalStreamBytes / (1024.0f * 1024.0f);
	Fraction = TotalMB > 0.0f ? FMath::Clamp(DeliveredMB / TotalMB, 0.0f, 1.0f) : 0.0f;
}

void USplatPakComponent::GetStreamStats(int32& OutViewLevel, int32& DrawnBlocks, int32& RedeliveredBlocks) const
{
	OutViewLevel = ViewLevel;
	DrawnBlocks = Runtime.IsValid() ? (int32)Runtime->FrameChunkCount() : 0;
	RedeliveredBlocks = Runtime.IsValid() ? (int32)Runtime->m_redeliveredBlocks : 0;
}

// The camera the stream follows: the player's in a game, the active level viewport's in the editor
bool USplatPakComponent::FindView(FVector& OutLocation, FRotator& OutRotation, float& OutFovYRadians, float& OutAspect, int32& OutHeightPixels) const
{
	const UWorld* World = GetWorld();
	if (!World) return false;
	if (World->IsGameWorld())
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		if (!PC || !PC->PlayerCameraManager) return false;
		const FMinimalViewInfo POV = PC->PlayerCameraManager->GetCameraCacheView();
		FVector2D Size(1920.0, 1080.0);
		if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Size);
		OutLocation = POV.Location;
		OutRotation = POV.Rotation;
		OutAspect = (float)FMath::Max(Size.X, 1.0) / (float)FMath::Max(Size.Y, 1.0);
		OutHeightPixels = (int32)FMath::Max(Size.Y, 1.0);
		OutFovYRadians = 2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(POV.FOV) * 0.5f) / OutAspect);
		return true;
	}
#if WITH_EDITOR
	if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->Viewport)
	{
		const FEditorViewportClient* VC = GCurrentLevelEditingViewportClient;
		const FIntPoint Size = VC->Viewport->GetSizeXY();
		if (Size.X <= 0 || Size.Y <= 0) return false;
		OutLocation = VC->GetViewLocation();
		OutRotation = VC->GetViewRotation();
		OutAspect = (float)Size.X / (float)Size.Y;
		OutHeightPixels = Size.Y;
		OutFovYRadians = 2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VC->ViewFOV) * 0.5f) / OutAspect);
		return true;
	}
#endif
	return false;
}

void USplatPakComponent::PushStreamSettings() const
{
	if (FSplatPakSceneProxy* Proxy = static_cast<FSplatPakSceneProxy*>(SceneProxy))
	{
		ENQUEUE_RENDER_COMMAND(SetSplatStreamSettings)([Proxy, bDither = bDitheredTransitions, bGlow = bRefinementVisualizer, Intensity = GlowIntensity, Hue = GlowHue, bFill = bShadowMeshFill, bGauss = bGaussians](FRHICommandListImmediate&)
		{
			Proxy->SetStreamSettings_RenderThread(bDither, bGlow, Intensity, Hue, bFill, bGauss);
		});
	}
}

void USplatPakComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateLoadingVeil(DeltaTime);
	if (!bStreaming || !SplatPakAsset)
	{
		if (Runtime.IsValid() || StreamLevels.IsValid() || RuntimeInit.IsValid()) StopStream();
		return;
	}

	// 1. Every level prepared (the asset, on a worker), then the runtime (on a worker); the resident level draws meanwhile
	if (!Runtime.IsValid())
	{
		if (bStreamFailed) return;
		if (!StreamLevels.IsValid())
		{
			FString Error;
			StreamLevels = SplatPakAsset->GetStreamLevels(&Error);
			if (!StreamLevels.IsValid())
			{
				if (!Error.IsEmpty())
				{
					UE_LOG(LogSplatPak, Warning, TEXT("%s: %s does not stream: %s"), *GetPathName(), *SplatPakAsset->GetName(), *Error);
					bStreamFailed = true;
				}
				return;
			}
		}
		if (!RuntimeInit.IsValid())
		{
			StartStream();
			return;
		}
		if (!RuntimeInit.IsReady()) return;
		const bool bOk = RuntimeInit.Get();
		RuntimeInit = TFuture<bool>();
		if (!bOk)
		{
			UE_LOG(LogSplatPak, Warning, TEXT("%s: %s holds no blocks to stream"), *GetPathName(), *SplatPakAsset->GetName());
			bStreamFailed = true;
			return;
		}
		Runtime = PendingRuntime;
		StreamSource = PendingSource;
		PendingRuntime.Reset();
		PendingSource.Reset();
		MarkRenderStateDirty(); // A new proxy takes the stream's records
		UE_LOG(LogSplatPak, Log, TEXT("%s: streaming %s: %d blocks, %.1f MB"), *GetPathName(), *SplatPakAsset->GetName(),
			(int32)Runtime->m_allStreamChunkPtrs.size(), Runtime->m_totalStreamBytes / (1024.0f * 1024.0f));
	}

	// 2. The view, in model space: the eye, the direction and the view-projection (Unreal's reversed, infinite depth)
	FVector Location;
	FRotator Rotation;
	float FovY = 0.0f, Aspect = 1.0f;
	int32 Height = 1080;
	if (!FindView(Location, Rotation, FovY, Aspect, Height)) return;
	const FMatrix ModelToWorld = GetModelToLocal() * GetComponentTransform().ToMatrixWithScale();
	const FMatrix WorldToModel = ModelToWorld.Inverse();
	const FMatrix ViewMatrix = FTranslationMatrix(-Location) * FInverseRotationMatrix(Rotation)
		* FMatrix(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
	const float HalfFovX = FMath::Atan(FMath::Tan(FovY * 0.5f) * Aspect);
	const FMatrix Projection = FReversedZPerspectiveMatrix(HalfFovX, Aspect, 1.0f, GNearClippingPlane);
	const FMatrix ModelToClip = ModelToWorld * ViewMatrix * Projection;
	LastModelToClip = ModelToClip;

	Splat::StreamCamera& Camera = Runtime->camera;
	Camera.explicitView = true;
	const FVector Eye = WorldToModel.TransformPosition(Location);
	const FVector Forward = WorldToModel.TransformVector(Rotation.Vector()).GetSafeNormal();
	Camera.eye = Splat::XMFLOAT3((float)Eye.X, (float)Eye.Y, (float)Eye.Z);
	Camera.forward = Splat::XMFLOAT3((float)Forward.X, (float)Forward.Y, (float)Forward.Z);
	for (int32 Row = 0; Row < 4; Row++)
		for (int32 Col = 0; Col < 4; Col++)
			Camera.viewProj.m[Row][Col] = (float)ModelToClip.M[Row][Col];
	Camera.fovY = FovY;
	Camera.height = (uint32)FMath::Max(Height, 1);
	Camera.width = (uint32)FMath::Max(1, FMath::RoundToInt(Height * Aspect));

	// 3. The model-wide level: SplatPak's auto distance LOD on the screen-space rule, with its hysteresis (step coarser once
	// the next level is a whole level past, finer a little before the current one is exceeded) and one step per cross-fade
	{
		const Splat::XMFLOAT3& C = Runtime->m_center;
		const float Distance = FMath::Sqrt(FMath::Square(Camera.eye.x - C.x) + FMath::Square(Camera.eye.y - C.y) + FMath::Square(Camera.eye.z - C.z));
		// Always finest: a spacing far below a pixel, so every block in view wants level 0
		Runtime->settings.lodPixelSpacing = bAlwaysFinest ? 0.01f : LodPixelSpacing;
		const int32 MaxLevel = (int32)Runtime->m_lodStreamChunks.size() - 1;
		LevelCooldown = FMath::Max(0.0f, LevelCooldown - DeltaTime);
		if (bAlwaysFinest) ViewLevel = 0;
		else if (LevelCooldown <= 0.0f && MaxLevel > 0)
		{
			const float Target = Runtime->ContinuousLodAtDistance(Distance, true);
			int32 NewLevel = ViewLevel;
			if (Target >= (float)ViewLevel + 1.0f) NewLevel = FMath::Min(MaxLevel, ViewLevel + 1);
			else if (Target < (float)ViewLevel - 0.15f) NewLevel = FMath::Max(0, ViewLevel - 1);
			if (NewLevel != ViewLevel)
			{
				ViewLevel = NewLevel;
				LevelCooldown = 0.75f;
			}
		}
	}

	// Held (SetStreamHold): ready, but not streaming yet; the resident level draws meanwhile
	if (bStreamHold)
	{
		PushStreamSettings();
		return;
	}

	// 4. The settings, as SplatPak's Streaming tab sets them, then one step of the stream
	Splat::StreamSettings& S = Runtime->settings;
	S.autoLOD = true;
	S.selectedPreviewLOD = ViewLevel;
	S.lodScreenSpace = true;
	S.lodPerBlock = bPerBlockLevels;
	S.enableStreamingSimulation = true;
	S.isStreamingPaused = false;
	S.unthrottledBandwidth = bUnthrottled;
	S.bandwidthThrottleMBps = BandwidthMBps;
	S.releaseUnusedSegments = bReleaseUnusedSegments;
	S.streamingPolicy = StreamingPolicy == ESplatPakStreamingPolicy::Conservative ? Splat::StreamSettings::Policy::Conservative : Splat::StreamSettings::Policy::Greedy;
	// Always finest: no cross-fades, so no refinement pacing either (the runtime caps the refinement starts per frame only
	// while dithering, which held Cthulhu at its coarser levels for many seconds after its finest blocks had arrived)
	S.enableDitheredTransitions = bDitheredTransitions && !bAlwaysFinest;
	S.ditherTransitionDurationSec = 0.75f;
	S.showChunkStream = bRefinementVisualizer;
	S.chunkStreamDuration = FadeDuration;
	S.prioritizeFrustumAndProximity = true;
	S.splatDrawAsDiscs = !bGaussians; // Gaussians cross-fade by opacity (dilationMorph), discs by dither (blendWeight)
	// An environment package (its view settings): no facing test (viewed from inside), and a residency ring of the core's radius around the
	// eye (3 to 30 m in the package's metres), as in SplatPak
	S.environmentScene = SplatPakAsset && SplatPakAsset->bEnvironmentScene;
	S.residencyRingRadius = S.environmentScene ? FMath::Clamp(SplatPakAsset->CoreRadius, 3.0f, 30.0f) : 0.0f;
	// An environment streams Conservative (the view and the ring, not the whole scene in the background), whatever the
	// Streaming Policy property says; that property applies to objects
	if (S.environmentScene) S.streamingPolicy = bGreedyEnvironment ? Splat::StreamSettings::Policy::Greedy : Splat::StreamSettings::Policy::Conservative;
	// The subject (where the training cameras converge) streams first
	if (SplatPakAsset && SplatPakAsset->SubjectRadius > 0.0f)
	{
		S.subjectCentre = Splat::XMFLOAT3(SplatPakAsset->SubjectCentre.X, SplatPakAsset->SubjectCentre.Y, SplatPakAsset->SubjectCentre.Z);
		S.subjectRadius = SplatPakAsset->SubjectRadius;
	}
	else S.subjectRadius = 0.0f;
	Runtime->UpdateStreamingSimulation(FMath::Min((double)DeltaTime, 0.1));

	// 5. The render list to the proxy
	if (Runtime->m_frameProduced)
	{
		if (FSplatPakSceneProxy* Proxy = static_cast<FSplatPakSceneProxy*>(SceneProxy))
		{
			const int32 Count = (int32)FMath::Min<size_t>(Runtime->FrameChunkCount(), Runtime->m_rendererMeshletChunks.size());
			TArray<SplatCore::FMeshlet> Meshlets;
			Meshlets.SetNumUninitialized(Count);
			if (Count > 0) memcpy(Meshlets.GetData(), Runtime->m_rendererMeshletChunks.data(), Count * sizeof(SplatCore::FMeshlet));
			ENQUEUE_RENDER_COMMAND(SetSplatStreamFrame)([Proxy, Meshlets = MoveTemp(Meshlets)](FRHICommandListImmediate&) mutable
			{
				Proxy->SetStreamFrame_RenderThread(MoveTemp(Meshlets));
			});
		}
	}
	PushStreamSettings();
}

#if WITH_EDITOR
void USplatPakComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName Name = PropertyChangedEvent.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(USplatPakComponent, EmissiveIntensity))
	{
		SetEmissiveIntensity(EmissiveIntensity);
		return;
	}
	static const FName LightingNames[] = {
		GET_MEMBER_NAME_CHECKED(USplatPakComponent, bLit), GET_MEMBER_NAME_CHECKED(USplatPakComponent, LitBlend),
		GET_MEMBER_NAME_CHECKED(USplatPakComponent, AlbedoScale), GET_MEMBER_NAME_CHECKED(USplatPakComponent, LitRoughness),
		GET_MEMBER_NAME_CHECKED(USplatPakComponent, LitSpecular), GET_MEMBER_NAME_CHECKED(USplatPakComponent, NormalSmoothing) };
	for (const FName& Setting : LightingNames)
	{
		if (Name == Setting)
		{
			PushLightingSettings();
			return;
		}
	}
	// The stream's own settings are read every tick: nothing to rebuild
	static const FName StreamSettingNames[] = {
		GET_MEMBER_NAME_CHECKED(USplatPakComponent, BandwidthMBps), GET_MEMBER_NAME_CHECKED(USplatPakComponent, bUnthrottled),
		GET_MEMBER_NAME_CHECKED(USplatPakComponent, bReleaseUnusedSegments), GET_MEMBER_NAME_CHECKED(USplatPakComponent, StreamingPolicy),
		GET_MEMBER_NAME_CHECKED(USplatPakComponent, bPerBlockLevels), GET_MEMBER_NAME_CHECKED(USplatPakComponent, LodPixelSpacing),
		GET_MEMBER_NAME_CHECKED(USplatPakComponent, bDitheredTransitions), GET_MEMBER_NAME_CHECKED(USplatPakComponent, bRefinementVisualizer),
		GET_MEMBER_NAME_CHECKED(USplatPakComponent, GlowIntensity), GET_MEMBER_NAME_CHECKED(USplatPakComponent, GlowHue),
		GET_MEMBER_NAME_CHECKED(USplatPakComponent, FadeDuration), GET_MEMBER_NAME_CHECKED(USplatPakComponent, bGaussians) };
	for (const FName& Setting : StreamSettingNames)
	{
		if (Name == Setting) return;
	}
	if (Name == GET_MEMBER_NAME_CHECKED(USplatPakComponent, SplatPakAsset)) StopStream();
	UpdateBounds();
	MarkRenderStateDirty();
	UpdateShadowMeshComponent();
}
#endif
