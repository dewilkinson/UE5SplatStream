// SplatPakComponent.h
// Places a USplatPakAsset in a level and draws it as opaque discs, depth-tested against the rest of the scene. With
// streaming on (the default) the model streams the way SplatPak streams it: the codec library's streaming runtime
// (StreamRuntime.h) decides, under a simulated bandwidth, which blocks of which levels are resident, follows the
// Unreal camera (the active level viewport in the editor, the player's camera in a game), and hands back the render
// list each frame: per-block levels, cross-fades, Release Unused LOD Segments, and the Refinement Visualizer's arrival
// glow. Off, one level of the package stays resident (ResidentLevel). docs/design/ue5-streaming.md in 3DGSStream.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Components/PrimitiveComponent.h"
#include "SplatPakComponent.generated.h"

class FSceneInterface;
class USplatPakAsset;
class UStaticMeshComponent;
struct FSplatPakStreamLevels;
struct FSplatPakStreamSource;
namespace Splat { class StreamRuntime; }

// What streams besides the view's own requests (StreamSettings::Policy in the codec)
UENUM(BlueprintType)
enum class ESplatPakStreamingPolicy : uint8
{
	Conservative UMETA(ToolTip = "Only what the view needs: the blocks in view on the side facing the camera, each down to its own level. Nothing in the background."),
	Greedy       UMETA(ToolTip = "Keeps loading in the background regardless of the view. With Release Unused LOD Segments: every block of every level down to the finest level in view, model-wide."),
};

// One level's residency, for a HUD
USTRUCT(BlueprintType)
struct FSplatPakLevelResidency
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "SplatPak") int32 Level = 0;
	UPROPERTY(BlueprintReadOnly, Category = "SplatPak") int32 ResidentBlocks = 0;
	UPROPERTY(BlueprintReadOnly, Category = "SplatPak") int32 TotalBlocks = 0;
	UPROPERTY(BlueprintReadOnly, Category = "SplatPak") float ResidentFraction = 0.0f; // Of the level's splats
};

UENUM(BlueprintType)
enum class ESplatPakOrientation : uint8
{
	Disc      UMETA(ToolTip = "Disc in each splat's tangent plane"),
	Billboard UMETA(ToolTip = "Camera-facing quad"),
};

UCLASS(ClassGroup = Rendering, meta = (BlueprintSpawnableComponent), hidecategories = (Collision, Physics, Navigation, Materials))
class SPLATPAK_API USplatPakComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	USplatPakComponent();

	UFUNCTION(BlueprintCallable, Category = "SplatPak")
	void SetSplatPakAsset(USplatPakAsset* NewAsset);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak")
	TObjectPtr<USplatPakAsset> SplatPakAsset;

	// LOD level kept resident with streaming off: 0 is the finest, -1 the coarsest the package holds. With streaming on,
	// the level drawn while every level is being prepared.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak", meta = (ClampMin = "-1"))
	int32 ResidentLevel = -1;

	// ---- Streaming (SplatPak's Streaming tab) ----

	// Streams the model: every level is decoded into memory when it loads (a simulated network, as in SplatPak) and
	// the streaming runtime decides what is resident and drawn under the bandwidth below.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Streaming")
	bool bStreaming = true;

	// The simulated link, MB/s (3G 0.2, 4G 2, 5G 7.5). Ignored when Unthrottled.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming", meta = (ClampMin = "0.05", UIMax = "100.0", EditCondition = "bStreaming && !bUnthrottled"))
	float BandwidthMBps = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming", meta = (EditCondition = "bStreaming"))
	bool bUnthrottled = false;

	// Evicts a level's segments as soon as nothing draws them any more: zoom out or turn away, and the finer segments
	// fade back to their coarser parents, then leave memory. The two coarsest levels always stay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming", meta = (EditCondition = "bStreaming"))
	bool bReleaseUnusedSegments = true;

	// For objects. An environment package (its view settings) streams Conservative, the view and a residency ring around
	// the eye, not the whole scene in the background, unless bGreedyEnvironment is set.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming", meta = (EditCondition = "bStreaming"))
	ESplatPakStreamingPolicy StreamingPolicy = ESplatPakStreamingPolicy::Greedy;

	// An environment streams Greedy too: the view first, then the rest of the scene in the background (a SoundStage's
	// backdrop, so the floors upstairs are in by the time a visitor climbs to them).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming", meta = (EditCondition = "bStreaming"))
	bool bGreedyEnvironment = false;

	// Draws the finest level (0) wherever it is in view, at any distance: a centrepiece at full detail the moment the
	// visitor looks its way. Out of view it may still release (Release Unused LOD Segments), and it streams back as soon
	// as it is in view again (ignores Lod Pixel Spacing)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming", meta = (EditCondition = "bStreaming"))
	bool bAlwaysFinest = false;

	// Each block takes its own level from its distance to the camera (near patches finer, far ones coarser).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming", meta = (EditCondition = "bStreaming"))
	bool bPerBlockLevels = true;

	// The screen-space level rule: a level is drawn once its points sit at most this many pixels apart.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming", meta = (ClampMin = "0.25", ClampMax = "8.0", EditCondition = "bStreaming"))
	float LodPixelSpacing = 1.0f;

	// Draws the stream as sorted 3D Gaussians, alpha-blended back to front over the scene, as SplatPak draws a splat
	// package (the coarse levels with their scales capped at 1.5 x the level's spacing). Off: opaque discs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming", meta = (EditCondition = "bStreaming"))
	bool bGaussians = true;

	// Dissolves each refinement and demotion with a dither (discs) or an opacity cross-fade (Gaussians) instead of
	// switching levels at once.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming", meta = (EditCondition = "bStreaming"))
	bool bDitheredTransitions = true;

	// Blurs the whole view while the stream initialises, until the scan's two coarsest levels are resident and drawing
	// (the scene whole, if coarse), then eases out over about half a second while the finer levels refine (SplatPak's
	// Loading Blur, SplatPakVeil.h). Armed when the scan appears and by Evict and Restream; gives up after Veil Timeout.
	// In game worlds only: play in editor and packaged games, not the editor viewport.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming|Loading Veil", meta = (EditCondition = "bStreaming"))
	bool bLoadingVeil = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming|Loading Veil", meta = (ClampMin = "1.0", UIMax = "60.0", EditCondition = "bStreaming && bLoadingVeil"))
	float VeilTimeout = 20.0f;

	// Veils the view again until it has loaded (as a new scan does).
	UFUNCTION(BlueprintCallable, Category = "SplatPak|Streaming|Loading Veil")
	void ArmLoadingVeil();

	// Drops the veil at once, until the next arm (an Evict that should be watched streaming back in, for one)
	UFUNCTION(BlueprintCallable, Category = "SplatPak|Streaming|Loading Veil")
	void CancelLoadingVeil();

	// The share of the view's drawn blocks at the two finest levels they need, 0..1 (refreshed four times a second
	// while the veil is armed), and the veil's strength now, 0..1.
	UFUNCTION(BlueprintPure, Category = "SplatPak|Streaming|Loading Veil")
	void GetLoadingVeil(float& ViewFill, float& Strength) const;

	// Tints every block as it arrives in its level's colour, fading over Fade Duration (SplatPak's Refinement Visualizer).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming|Refinement Visualizer", meta = (EditCondition = "bStreaming"))
	bool bRefinementVisualizer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming|Refinement Visualizer", meta = (ClampMin = "0.0", ClampMax = "2.0", EditCondition = "bStreaming && bRefinementVisualizer"))
	float GlowIntensity = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming|Refinement Visualizer", meta = (ClampMin = "0.0", ClampMax = "360.0", EditCondition = "bStreaming && bRefinementVisualizer"))
	float GlowHue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Streaming|Refinement Visualizer", meta = (ClampMin = "0.1", ClampMax = "10.0", EditCondition = "bStreaming && bRefinementVisualizer"))
	float FadeDuration = 2.5f;

	// Drops everything but the two coarsest levels, so the model streams in again (SplatPak's Evict).
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "SplatPak|Streaming")
	void Evict();

	// Starts the stream again from nothing but the two coarsest levels and resets the counters.
	UFUNCTION(BlueprintCallable, Category = "SplatPak|Streaming")
	void Restream();

	// True once every level is prepared and the model streams.
	UFUNCTION(BlueprintPure, Category = "SplatPak|Streaming")
	bool IsStreamReady() const;

	// Each level's residency, finest first (empty until the stream is ready).
	UFUNCTION(BlueprintPure, Category = "SplatPak|Streaming")
	TArray<FSplatPakLevelResidency> GetLevelResidency() const;

	// Delivered and total bytes of the stream, in MB; the share delivered.
	UFUNCTION(BlueprintPure, Category = "SplatPak|Streaming")
	void GetStreamProgress(float& DeliveredMB, float& TotalMB, float& Fraction) const;

	// The level the view renders (model-wide), the blocks drawn this frame, and blocks delivered more than once.
	UFUNCTION(BlueprintPure, Category = "SplatPak|Streaming")
	void GetStreamStats(int32& ViewLevel, int32& DrawnBlocks, int32& RedeliveredBlocks) const;

	// The asset's training camera Index (its view settings) placed in the world through this component: a camera
	// there with this vertical field of view (degrees) sees the scan as it was trained. False without that camera.
	UFUNCTION(BlueprintPure, Category = "SplatPak|View")
	bool GetTrainingCamera(int32 Index, FTransform& WorldTransform, float& FovYDegrees) const;

	// Multiplies every disc radius (g_Radius in SplatPak).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float SplatScale = 1.0f;

	// Disc radius as a multiple of the level's median point spacing (Auto Splat Size coverage).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float Coverage = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak")
	ESplatPakOrientation Orientation = ESplatPakOrientation::Disc;

	// Brightness of the scan's stored colours. They're baked light, drawn like an emissive surface,
	// so Unreal's exposure affects them: raise this until the scan looks right in your lighting.
	// Changing it is cheap (no rebuild, no re-upload) and it can be animated in Sequencer.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "SplatPak", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0", SliderExponent = "2.0"))
	float EmissiveIntensity = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "SplatPak")
	void SetEmissiveIntensity(float NewIntensity);

	// Lights the scan with Unreal's lights: the discs go into the GBuffer after the base pass, with each disc's normal (a
	// point record's stored normal, a splat's shortest axis), so directional, point and spot lights, their shadows (virtual
	// shadow maps included) and Lumen shade them. Always draws discs, never Gaussians, and without the gap fill. Needs the
	// deferred renderer without Substrate; otherwise the scan draws unlit as before.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Lighting")
	bool bLit = false;

	// The stored colour is baked light, so it is split: 0 keeps all of it emissive (the unlit look), 1 makes all of it the
	// albedo Unreal's lights shade. In between mixes the two, which hides how far the baked light differs from the scene's.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bLit"))
	float LitBlend = 1.0f;

	// The baked colour times this is the albedo (the stored colours already carry the capture's light).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Lighting", meta = (ClampMin = "0.0", UIMax = "4.0", EditCondition = "bLit"))
	float AlbedoScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bLit"))
	float LitRoughness = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bLit"))
	float LitSpecular = 0.5f;

	// The splats' own normals are noisy. Where a disc lies on the shadow mesh's surface, its normal leans this far toward the
	// mesh's smooth one: 0 keeps the splat's, 1 takes the mesh's. Needs a package with a shadow mesh (format 12 or later).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Lighting", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bLit"))
	float NormalSmoothing = 0.7f;

	UFUNCTION(BlueprintCallable, Category = "SplatPak|Lighting")
	void SetLighting(bool bNewLit, float NewLitBlend, float NewAlbedoScale, float NewRoughness, float NewSpecular);

	// The package's shadow mesh (USplatPakAsset::ShadowMesh), placed hidden: it casts the model's shadow from every light
	// type, virtual shadow maps included. Packages without one (format 11 or older) cast no shadow.
	// Draws the shadow mesh behind the splats in their colours, over the occluder mesh, so the gaps between sparse splats
	// read as surface instead of the background (SplatPak's gap fill). The occluder also hides the splats on the far side.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Shadow Mesh")
	bool bShadowMeshFill = true;

	// Without the gap fill: the occluder mesh alone into depth (no colour), a little behind where it is, so objects behind
	// this scan's walls are hidden from view (its Gaussians write no depth) but a piece standing against a wall is not
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SplatPak|Shadow Mesh", meta = (EditCondition = "!bShadowMeshFill"))
	bool bOccluderDepth = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Shadow Mesh")
	bool bShadowMeshCastShadow = true;

	// Lets the hidden shadow mesh contribute to Lumen and distance-field lighting (its mesh distance field).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Shadow Mesh")
	bool bShadowMeshAffectLumen = true;

	// Gives the model collision from the shadow mesh (line traces and physics); off, traces pass through.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Shadow Mesh")
	bool bShadowMeshCollision = true;

	// The hidden component holding the shadow mesh (transient: rebuilt whenever this component registers).
	UFUNCTION(BlueprintPure, Category = "SplatPak|Shadow Mesh")
	UStaticMeshComponent* GetShadowMeshComponent() const { return ShadowMeshComponent; }

	// A drop shadow on a floor: the shadow mesh (and DropShadowBox, a solid such as the plinth under it) projected along
	// DropShadowDirection onto the plane at DropShadowFloorZ, drawn after the splats as a soft darkening. An effect, not a
	// light: the scans around are baked light and take no shadows. Needs a package with a shadow mesh.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Drop Shadow")
	bool bDropShadow = false;

	// The way the light travels, world space (it must point down)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Drop Shadow", meta = (EditCondition = "bDropShadow"))
	FVector DropShadowDirection = FVector(0.6, 0.5, -0.6);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Drop Shadow", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bDropShadow"))
	float DropShadowOpacity = 0.5f;

	// The floor's height, world Z (cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Drop Shadow", meta = (EditCondition = "bDropShadow"))
	float DropShadowFloorZ = 0.0f;

	// A box that casts with the shadow mesh, world space (invalid: none)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Drop Shadow", meta = (EditCondition = "bDropShadow"))
	FBox DropShadowBox = FBox(ForceInit);

	UFUNCTION(BlueprintCallable, Category = "SplatPak|Drop Shadow")
	void SetDropShadow(bool bEnabled, FVector Direction, float Opacity, float FloorZ, FBox Box);

	// A colour grade on the stored colour, in linear space: the colour times ColourGain, its saturation scaled by
	// ColourSaturation about its luminance, plus ColourOffset (RGB; alpha unused). MatchColours sets it; it can also be
	// set by hand.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Colour Match")
	FLinearColor ColourGain = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Colour Match")
	FLinearColor ColourOffset = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Colour Match", meta = (ClampMin = "0.0"))
	float ColourSaturation = 1.0f;

	// Grades this scan's colours toward the scene around it, so an object captured under other light and white balance
	// sits in the room rather than on top of it: a gain per channel, in linear colour, moving its mean colour toward
	// that of the Scene scan within RadiusCm of this component while keeping its own contrast (sampled from both packages' shadow
	// mesh vertex colours), by Strength (0 = as captured, 1 = fully matched). False if either has no shadow mesh.
	UFUNCTION(BlueprintCallable, Category = "SplatPak|Colour Match")
	bool MatchColours(USplatPakComponent* Scene, float Strength, float RadiusCm = 300.0f);

	UFUNCTION(BlueprintCallable, Category = "SplatPak|Colour Match")
	void ClearColourMatch();

	// The scene's mean colour around this component (linear), as the last MatchColours measured it; black before
	UFUNCTION(BlueprintPure, Category = "SplatPak|Colour Match")
	FLinearColor GetMatchedRoomColour() const { return MatchedRoomColour; }

	// Holds the stream: the levels are prepared, but nothing is requested until it is released, so a scene can bring its
	// scans in one after another (ASoundStageActor: the backdrop's view, then its pieces in their stream order)
	UFUNCTION(BlueprintCallable, Category = "SplatPak|Streaming")
	void SetStreamHold(bool bHold);

	// Whether the view's blocks of this scan are drawn at the two finest levels they need (bFinest: at the finest), for at least Threshold of them
	// (true when none of it is in view); false while held or before the stream draws
	UFUNCTION(BlueprintPure, Category = "SplatPak|Streaming")
	bool IsViewLoaded(float Threshold = 0.9f, bool bFinest = false) const;

	// Package units to Unreal centimetres (packages are in metres).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Import Frame", meta = (ClampMin = "0.0001"))
	float UnitsToCentimeters = 100.0f;

	// Packages are right-handed, Y-up; Unreal is left-handed, Z-up. Swapping Y and Z converts both.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SplatPak|Import Frame")
	bool bConvertYUpToZUp = true;

	// Package model space to this component's local space.
	FMatrix GetModelToLocal() const;

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//~ UPrimitiveComponent
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void UpdateShadowMeshComponent();
	void StartStream();                       // Every level ready: the runtime, from nothing but the two coarsest levels
	void StopStream();
	bool FindView(FVector& OutLocation, FRotator& OutRotation, float& OutFovYRadians, float& OutAspect, int32& OutHeightPixels) const;
	void PushStreamSettings() const;          // The draw settings that change without a new proxy
	void PushLightingSettings() const;        // Lit mode and its material values, also without a new proxy
	void UpdateLoadingVeil(float DeltaTime);  // The view fill and the veil's strength, published for the world's scene
	void PublishVeil(float Strength);
	float ComputeViewFill(int32& OutInView, int32 Slack) const; // The share of the in-view drawn blocks within Slack levels of their target
	bool bStreamHold = false;
	FLinearColor MatchedRoomColour = FLinearColor::Black;

	TSharedPtr<const FSplatPakStreamLevels, ESPMode::ThreadSafe> StreamLevels;
	TSharedPtr<const FSplatPakStreamSource, ESPMode::ThreadSafe> StreamSource; // The runtime's records, for the proxy
	TSharedPtr<Splat::StreamRuntime> Runtime;
	TFuture<bool> RuntimeInit;                // The runtime being set up on a worker (StartStream)
	TSharedPtr<Splat::StreamRuntime> PendingRuntime;
	TSharedPtr<FSplatPakStreamSource, ESPMode::ThreadSafe> PendingSource;
	int32 ViewLevel = 0;                      // The model-wide level (SplatPak's auto distance LOD, with its hysteresis)
	float LevelCooldown = 0.0f;
	bool bStreamFailed = false;
	FMatrix LastModelToClip = FMatrix::Identity; // The view the stream last stepped with, for the veil's in-view test
	bool bVeilArmed = false;
	float VeilSeconds = 0.0f;                 // Since the veil was armed
	float VeilReadySeconds = 0.0f;            // Of those, since the stream started
	float VeilFill = 0.0f;
	float VeilFillTimer = 0.0f;
	float VeilStrength = 0.0f;                // Eased toward the fill's target, so the blur does not step
	const FSceneInterface* VeilScene = nullptr; // Where VeilStrength is published

	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
	TObjectPtr<UStaticMeshComponent> ShadowMeshComponent;
};
