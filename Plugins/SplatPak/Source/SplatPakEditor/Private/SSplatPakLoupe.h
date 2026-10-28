// SSplatPakLoupe.h
// The loupe, as in SplatPak (L there): what is under the mouse in the level viewport, drawn again magnified into an inset
// in the viewport's bottom right. It is a real second render (a scene capture along the cursor's ray with a narrower
// field of view), not stretched pixels, so splats keep their edges at any zoom. The wheel over the inset zooms it, a left
// click cycles what it shows (final colour, base colour, world normal, device depth), a right click pins it to the view
// it has. In play-in-editor it follows the player's camera and looks at the centre of the view.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UWorld;

namespace SplatPakLoupe
{
	// Shows or hides the inset over the first active level viewport
	void SetEnabled(bool bEnabled);
	bool IsEnabled();
	void Shutdown();

	// Pixel magnification: the patch under the mouse is the inset's size divided by it
	float GetZoom();
	void SetZoom(float Zoom);

	// What the inset shows (a scene capture source)
	ESceneCaptureSource GetMode();
	void SetMode(ESceneCaptureSource Mode);
	FText GetModeName(ESceneCaptureSource Mode);
	const TArray<ESceneCaptureSource>& GetModes();

	// Pinned: the inset keeps the direction it had instead of following the mouse
	bool IsPinned();
	void SetPinned(bool bPinned);
}

class SSplatPakLoupe : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SSplatPakLoupe) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SSplatPakLoupe() override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SSplatPakLoupe"); }

	static constexpr int32 Width = 320; // The app's default inset (user, 2026-10-28: small)
	static constexpr int32 Height = 220;

private:
	bool UpdateView(FVector& OutEye, FVector& OutDir, FVector& OutUp, float& OutTanHalfFov, UWorld*& OutWorld);

	TObjectPtr<UTextureRenderTarget2D> Target;
	TObjectPtr<USceneCaptureComponent2D> Capture;
	FSlateBrush Brush;
	TWeakObjectPtr<UWorld> CaptureWorld;
	FVector PinnedDir = FVector::ForwardVector;
};
