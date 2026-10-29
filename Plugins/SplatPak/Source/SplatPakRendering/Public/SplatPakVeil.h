// SplatPakVeil.h
// The loading veil, as in SplatPak (its Loading Blur): the whole view blurred by a Gaussian while a scan initialises and
// until the view's blocks have arrived (USplatPakComponent::bLoadingVeil decides when), easing out as they do. Each
// component publishes its strength for its world's scene; a view is blurred by the strongest in its scene, by up to
// r.SplatPak.Veil.Size of its height, after the splat passes (FSplatPakViewExtension). r.SplatPak.Veil 0 turns it off.

#pragma once

#include "CoreMinimal.h"

class FRDGBuilder;
class FRDGTexture;
class FSceneInterface;
class FViewInfo;

namespace SplatPakVeil
{
	// Strength 0..1 for this source in this scene (game thread); 0 removes the source
	SPLATPAKRENDERING_API void SetStrength(const FSceneInterface* Scene, const void* Source, float Strength);

	// The strongest source's strength in this scene (any thread)
	SPLATPAKRENDERING_API float GetStrength(const FSceneInterface* Scene);

	// Blurs the view's rectangle of SceneColor in place when its scene is veiled (render thread)
	void AddPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTexture* SceneColor);

	// Blurs the view's rectangle of a render-targetable texture in place by a Gaussian of Sigma pixels (render thread;
	// the drop shadows use it too)
	void AddBlur(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTexture* Texture, float Sigma);
}
