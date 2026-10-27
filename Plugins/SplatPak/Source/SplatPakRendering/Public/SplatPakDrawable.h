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
	// Streaming only (FSplatPakStreamSource): the cross-fade dither and the Refinement Visualizer's arrival glow
	bool  bDitheredTransitions = true;
	bool  bRefinementVisualizer = true;
	float GlowIntensity = 0.25f; // SplatPak's default
	float GlowHue = 0.0f;        // Degrees
	// The occluder and shadow meshes behind the splats (SplatPak's gap fill): the occluder writes depth, the shadow mesh
	// covers the gaps between sparse splats in their colours
	bool  bShadowMeshFill = true;
	// Streaming only: draw as sorted 3D Gaussians, alpha-blended back to front (as SplatPak draws a splat package); off,
	// as opaque discs
	bool  bGaussians = true;
};

// Streaming (USplatPakComponent with streaming on): every level's records in the streaming runtime's order, uploaded once,
// and each level's point spacing for the disc radius. The meshlets each frame draws are the runtime's render list,
// handed over with SetStreamFrame_RenderThread; their SplatOffset indexes these records.
struct FSplatPakStreamSource
{
	std::vector<uint32_t> RecordWords; // 20-byte splat records, five words each
	float LevelSpacing[8] = {};        // Median point spacing per level (model units); 0 = unknown
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

	// Render thread: settings that change without rebuilding the proxy or re-uploading buffers.
	void SetEmissiveIntensity_RenderThread(float Intensity) { Settings.EmissiveIntensity = Intensity; }
	void SetStreamSettings_RenderThread(bool bDither, bool bVisualizer, float Intensity, float Hue, bool bFill, bool bGauss)
	{
		Settings.bDitheredTransitions = bDither; Settings.bRefinementVisualizer = bVisualizer; Settings.GlowIntensity = Intensity; Settings.GlowHue = Hue;
		Settings.bShadowMeshFill = bFill; Settings.bGaussians = bGauss;
	}

	// Render thread: streaming. The records (once), then each frame's render list. Until the first list arrives the
	// drawable draws its resident level.
	void SetStreamSource_RenderThread(FSplatPakStreamSourcePtr Source);
	void SetStreamFrame_RenderThread(TArray<SplatCore::FMeshlet>&& Meshlets);

	static const TArray<FSplatPakDrawable*>& GetRegisteredDrawables();

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
	mutable TRefCountPtr<FRDGPooledBuffer> OccluderColourBuffer;  // The gap fill: the occluder's colours, and the shadow mesh
	mutable TRefCountPtr<FRDGPooledBuffer> ShadowPositionBuffer;
	mutable TRefCountPtr<FRDGPooledBuffer> ShadowColourBuffer;
	mutable TRefCountPtr<FRDGPooledBuffer> ShadowIndexBuffer;
	void AddGaussianPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGBufferRef Records, FRDGBufferRef Meshlets, const class FSplatPakDiscParameters& Disc,
		FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth) const;
	void AddMeshFillPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMatrix44f& ModelToClip, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth) const;
	bool bRegistered = false;
};
