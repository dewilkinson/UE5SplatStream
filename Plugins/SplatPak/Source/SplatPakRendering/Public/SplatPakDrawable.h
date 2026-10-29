// SplatPakDrawable.h
// The render-thread half of a placed splat model: the decoded level, its GPU buffers and the
// draw settings. The scene proxy (SplatPak) derives from this and FPrimitiveSceneProxy and
// registers itself; FSplatPakViewExtension then draws every registered drawable each frame.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "SplatPakPackageCore.h"

class FRDGBuilder;
class FSceneView;
class FViewInfo;

using FSplatPakLevelPtr = TSharedPtr<const SplatCore::FDecodedLevel, ESPMode::ThreadSafe>;

enum class ESplatPakOrient : uint32
{
	Disc = 0,      // Disc in the splat's tangent plane
	Billboard = 1, // Camera-facing quad
};

struct FSplatPakDrawSettings
{
	float SplatScale = 1.0f;  // g_Radius in SplatPak: multiplies every disc radius
	float Coverage = 1.2f;    // Disc radius as a multiple of the level's point spacing (Auto Splat Size)
	ESplatPakOrient Orient = ESplatPakOrient::Disc;
	float EmissiveIntensity = 1.0f; // Multiplies the stored colour, which is drawn as emissive light
	// A colour grade on the stored colour in linear space: colour * Gain, its saturation scaled by Saturation about its
	// luminance, + Offset (USplatPakComponent::MatchColours fits it to the scene around an object, so a sculpture's own
	// capture lighting, white balance and saturation meet the room's)
	FVector3f ColourGain = FVector3f::OneVector;
	FVector3f ColourOffset = FVector3f::ZeroVector;
	float ColourSaturation = 1.0f;
	// Streaming only (FSplatPakStreamSource): the cross-fade dither and the Refinement Visualizer's arrival glow
	bool  bDitheredTransitions = true;
	bool  bRefinementVisualizer = true;
	float GlowIntensity = 0.25f; // SplatPak's default
	float GlowHue = 0.0f;        // Degrees
	// The occluder and shadow meshes behind the splats (SplatPak's gap fill): the occluder writes depth, the shadow mesh
	// covers the gaps between sparse splats in their colours
	bool  bShadowMeshFill = true;
	// Without the fill: the occluder alone, into depth only (no colour), pushed back from the eye by OccluderPushBack of
	// its distance, so objects behind the scan's walls are hidden (Gaussians write no depth of their own)
	bool  bOccluderDepth = false;
	float OccluderPushBack = 0.03f;
	// Streaming only: draw as sorted 3D Gaussians, alpha-blended back to front (as SplatPak draws a splat package); off,
	// as opaque discs
	bool  bGaussians = true;
	// An environment scan (viewed from inside, around the objects placed in it): drawn before the objects, since the
	// Gaussians write no depth and a scan drawn later would blend its background over them
	bool  bEnvironment = false;
	// A drop shadow (SplatPakDropShadow.h): the shadow mesh, and a box in world space, projected along the direction
	// (world, normalised, pointing down) onto the plane z = FloorZ, as a soft darkening of what the view drew
	bool  bDropShadow = false;
	FVector3f DropShadowDirection = FVector3f(0.0f, 0.0f, -1.0f);
	float DropShadowOpacity = 0.5f;
	float DropShadowFloorZ = 0.0f;
	bool  bDropShadowBox = false;
	FVector3f DropShadowBoxMin = FVector3f::ZeroVector;
	FVector3f DropShadowBoxMax = FVector3f::ZeroVector;
	// Lit mode: the discs are written into Unreal's GBuffer after the base pass, so its lights, shadows and Lumen shade
	// them. Always discs (never Gaussians), without the gap fill; deferred shading without Substrate only, else unlit.
	bool  bLit = false;
	float LitBlend = 1.0f;      // 0 = the baked colour as emissive (the unlit look), 1 = the colour as albedo, fully lit
	float LitRoughness = 0.8f;
	float LitSpecular = 0.5f;
	float AlbedoScale = 1.0f;   // The baked colour times this is the albedo
	float NormalSmoothing = 0.7f; // 0 = each splat's own normal, 1 = the shadow mesh's smooth normal where a disc lies on it
};

// Streaming (USplatPakComponent with streaming on): every level's records in the streaming runtime's order, uploaded once,
// and each level's point spacing for the disc radius. The meshlets each frame draws are the runtime's render list,
// handed over with SetStreamFrame_RenderThread; their SplatOffset indexes these records.
struct FSplatPakStreamSource
{
	std::vector<uint32_t> RecordWords; // 20-byte splat records, five words each
	float LevelSpacing[8] = {};        // Median point spacing per level (model units); 0 = unknown
	// The harmonics (view-dependent colour), when read: SHRecordBytes signed bytes per splat in the records' order,
	// packed into words; SHDegree 0 and no words when not
	std::vector<uint32_t> SHWords;
	uint32_t SHDegree = 0;
	uint32_t SHRecordBytes = 0;
};
using FSplatPakStreamSourcePtr = TSharedPtr<const FSplatPakStreamSource, ESPMode::ThreadSafe>;

class SPLATPAKRENDERING_API FSplatPakDrawable
{
public:
	FSplatPakDrawable(FSplatPakLevelPtr InLevel, const FSplatPakDrawSettings& InSettings);
	virtual ~FSplatPakDrawable();

	// Render thread. Adds/removes this drawable from the set FSplatPakViewExtension draws.
	void RegisterDrawable();
	void UnregisterDrawable();

	// Render thread: true if this drawable belongs to the view's scene and is visible in it.
	virtual bool IsVisibleInView(const FSceneView& View) const = 0;

	// Render thread: model space (the package's own frame and units) to UE world space.
	virtual FMatrix GetModelToWorld() const = 0;

	// Render thread: the opaque disc pass, depth-tested and depth-writing against the scene.
	void AddDiscPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth) const;

	// Render thread: whether this drawable draws lit in the view (lit mode on, deferred shading, no Substrate). A lit
	// drawable draws with AddDiscGBufferPass after the base pass instead of AddDiscPass.
	bool DrawsLit(const FSceneView& View) const;

	// Render thread: SplatPak's splat display for this scan's Gaussians in the view: 0 normal, 1 splat outlines, 2 a random
	// colour per splat, 3 splat centres (r.SplatPak.SplatViz, and r.SplatPak.Loupe for scene captures)
	int32 GetSplatViz(const FSceneView& View) const;

	// Render thread: SplatPak's own buffer view (r.SplatPak.DebugView; 0 = the final colour)
	static int32 GetDebugView_RenderThread();

	// Render thread: lit mode. The discs into the base pass's scene colour and GBufferA-C (BasePassTargets), depth-tested
	// and depth-writing.
	void AddDiscGBufferPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const struct FRenderTargetBindingSlots& BasePassTargets) const;

	// Render thread: settings that change without rebuilding the proxy or re-uploading buffers.
	void SetEmissiveIntensity_RenderThread(float Intensity) { Settings.EmissiveIntensity = Intensity; }
	void SetStreamSettings_RenderThread(bool bDither, bool bVisualizer, float Intensity, float Hue, bool bFill, bool bGauss)
	{
		Settings.bDitheredTransitions = bDither; Settings.bRefinementVisualizer = bVisualizer; Settings.GlowIntensity = Intensity; Settings.GlowHue = Hue;
		Settings.bShadowMeshFill = bFill; Settings.bGaussians = bGauss;
	}
	void SetLightingSettings_RenderThread(bool bLit, float Blend, float Roughness, float Specular, float Albedo, float Smoothing)
	{
		Settings.bLit = bLit; Settings.LitBlend = Blend; Settings.LitRoughness = Roughness; Settings.LitSpecular = Specular; Settings.AlbedoScale = Albedo;
		Settings.NormalSmoothing = Smoothing;
	}

	// Render thread: streaming. The records (once), then each frame's render list. Until the first list arrives the
	// drawable draws its resident level.
	void SetStreamSource_RenderThread(FSplatPakStreamSourcePtr Source);
	void SetStreamFrame_RenderThread(TArray<SplatCore::FMeshlet>&& Meshlets);

	static const TArray<FSplatPakDrawable*>& GetRegisteredDrawables();

	bool IsEnvironment() const { return Settings.bEnvironment; }

	// Render thread: whether this drawable casts a drop shadow, and its silhouette on the floor into Mask (R8, max
	// blended, depth-tested against SceneDepth so it hides behind opaque geometry). SplatPakDropShadow.cpp.
	bool HasDropShadow() const { return Settings.bDropShadow; }
	void AddDropShadowMask(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef Mask, FRDGTextureRef SceneDepth) const;

	uint32 GetGPUMemoryBytes() const;

protected:
	FSplatPakLevelPtr Level;
	FSplatPakDrawSettings Settings;
	FSplatPakStreamSourcePtr Stream;
	TArray<SplatCore::FMeshlet> StreamMeshlets;
	TArray<uint32> StreamSlotBase;   // First Gaussian slot of each meshlet of the frame (exclusive prefix of SplatCount)
	uint32 StreamSlotCount = 0;

private:
	// Uploaded on first draw, then reused every frame.
	mutable TRefCountPtr<FRDGPooledBuffer> RecordsBuffer;
	mutable TRefCountPtr<FRDGPooledBuffer> MeshletBuffer;
	mutable TRefCountPtr<FRDGPooledBuffer> OccluderPositionBuffer; // The occluder mesh (FDecodedLevel), when the package has one
	mutable TRefCountPtr<FRDGPooledBuffer> OccluderIndexBuffer;
	mutable TRefCountPtr<FRDGPooledBuffer> StreamRecordsBuffer;   // FSplatPakStreamSource::RecordWords
	mutable TRefCountPtr<FRDGPooledBuffer> StreamSHBuffer;        // FSplatPakStreamSource::SHWords
	mutable TRefCountPtr<FRDGPooledBuffer> OccluderColourBuffer;  // The gap fill: the occluder's colours, and the shadow mesh
	mutable TRefCountPtr<FRDGPooledBuffer> ShadowPositionBuffer;
	mutable TRefCountPtr<FRDGPooledBuffer> ShadowColourBuffer;
	mutable TRefCountPtr<FRDGPooledBuffer> ShadowIndexBuffer;
	mutable TRefCountPtr<FRDGPooledBuffer> ShadowNormalBuffer;    // Lit mode: the shadow mesh's smooth vertex normals
	mutable TRefCountPtr<FRDGPooledBuffer> DropShadowBoxPositions; // The drop shadow's box: its eight corners (world) and twelve triangles
	mutable TRefCountPtr<FRDGPooledBuffer> DropShadowBoxIndices;
	mutable std::vector<float> ShadowNormals;                      // Their CPU copy, computed on first use (the upload reads it)
	// Lit mode: the shadow mesh's smooth world normals and clip w into a screen-sized target; null without a shadow mesh
	FRDGTextureRef AddMeshNormalPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMatrix44f& ModelToClip, const FMatrix44f& ModelToWorldNormal,
		FRDGTextureRef SceneDepth) const;
	void AddGaussianPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGBufferRef Records, FRDGBufferRef Meshlets, const class FSplatPakDiscParameters& Disc,
		FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth) const;
	void AddMeshFillPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMatrix44f& ModelToClip, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth) const;
	void AddDiscs(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth,
		const struct FRenderTargetBindingSlots* GBufferTargets) const;
	bool bRegistered = false;
};
