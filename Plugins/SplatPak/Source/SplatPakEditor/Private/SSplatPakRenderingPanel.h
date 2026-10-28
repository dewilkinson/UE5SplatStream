// SSplatPakRenderingPanel.h
// The SplatPak Rendering tab (Tools menu): rendering switches for every splat in the open worlds (editor and play in
// editor), the splat passes one at a time (r.SplatPak.Pass.*), visualizers for those passes (the viewport's GBuffer
// views, the meshlets occlusion culling skips, the hidden shadow mesh, the refinement glow), the SoundStage volumes,
// and the loupe inset (SSplatPakLoupe).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SSplatPakRenderingPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSplatPakRenderingPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static const FName TabName;
};
