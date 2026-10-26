// SplatDrawable.h
// The render-thread half of a placed splat model: the decoded level, its GPU buffers and the
// draw settings. The scene proxy (SplatPak) derives from this and FPrimitiveSceneProxy and
// registers itself; FSplatViewExtension then draws every registered drawable each frame.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "SplatPackageCore.h"

class FRDGBuilder;
class FSceneView;
class FViewInfo;

using FSplatLevelPtr = TSharedPtr<const SplatCore::FDecodedLevel, ESPMode::ThreadSafe>;

enum class ESplatOrient : uint32
{
	Disc = 0,      // Disc in the splat's tangent plane
	Billboard = 1, // Camera-facing quad
};

struct FSplatDrawSettings
{
	float SplatScale = 1.0f;  // g_Radius in SplatPak: multiplies every disc radius
	float Coverage = 1.2f;    // Disc radius as a multiple of the level's point spacing (Auto Splat Size)
	ESplatOrient Orient = ESplatOrient::Disc;
	float EmissiveIntensity = 1.0f; // Multiplies the stored colour, which is drawn as emissive light
};

class SPLATPAKRENDERING_API FSplatDrawable
{
public:
	FSplatDrawable(FSplatLevelPtr InLevel, const FSplatDrawSettings& InSettings);
	virtual ~FSplatDrawable();

	// Render thread. Adds/removes this drawable from the set FSplatViewExtension draws.
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

	static const TArray<FSplatDrawable*>& GetRegisteredDrawables();

	uint32 GetGPUMemoryBytes() const;

protected:
	FSplatLevelPtr Level;
	FSplatDrawSettings Settings;

private:
	// Uploaded on first draw, then reused every frame.
	mutable TRefCountPtr<FRDGPooledBuffer> RecordsBuffer;
	mutable TRefCountPtr<FRDGPooledBuffer> MeshletBuffer;
	bool bRegistered = false;
};
