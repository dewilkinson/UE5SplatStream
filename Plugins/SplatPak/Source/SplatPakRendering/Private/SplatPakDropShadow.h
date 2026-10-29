// SplatPakDropShadow.h
// Drop shadows for objects placed in a splat scene (a SoundStage's pieces): the scans are baked light and take no
// shadows from Unreal's lights, so a sculpture's shadow on the floor is drawn as an effect instead. Each drawable with
// bDropShadow flattens its shadow mesh (and a box, its plinth) onto the plane z = FloorZ along its direction into one R8
// mask, depth-tested so it hides behind opaque geometry; the mask is blurred (r.SplatPak.DropShadow.Blur) and multiplies
// the view's colour, after the splat passes. r.SplatPak.DropShadow 0 turns them off.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class FSplatPakDrawable;
class FViewInfo;

namespace SplatPakDropShadow
{
	// Render thread, after the splat passes
	void AddPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, TConstArrayView<const FSplatPakDrawable*> Drawables, FRDGTextureRef SceneColor,
		FRDGTextureRef SceneDepth);

	// The projection onto the plane z = FloorZ along Direction (world, pointing down), as a row-vector matrix
	FMatrix FloorProjection(const FVector& Direction, double FloorZ);
}
