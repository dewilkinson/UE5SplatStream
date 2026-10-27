// SplatPakComponent.cpp

#include "SplatPakComponent.h"

#include "Async/Async.h"
#include "SplatPakAsset.h"
#include "SplatPakSceneProxy.h"
#include "SplatPakStreamLevels.h"
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
		UpdateBounds();
		MarkRenderStateDirty();
		UpdateShadowMeshComponent();
	}
}

void USplatPakComponent::OnRegister()
{
	Super::OnRegister();
	UpdateShadowMeshComponent();
}

void USplatPakComponent::OnUnregister()
{
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
	Settings.bGaussians = bGaussians;
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
		Runtime->settings.lodPixelSpacing = LodPixelSpacing;
		const int32 MaxLevel = (int32)Runtime->m_lodStreamChunks.size() - 1;
		LevelCooldown = FMath::Max(0.0f, LevelCooldown - DeltaTime);
		if (LevelCooldown <= 0.0f && MaxLevel > 0)
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
	S.enableDitheredTransitions = bDitheredTransitions;
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
	if (S.environmentScene) S.streamingPolicy = Splat::StreamSettings::Policy::Conservative;
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
