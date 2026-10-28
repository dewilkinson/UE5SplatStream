// SSplatPakLoupe.cpp

#include "SSplatPakLoupe.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "SLevelViewport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SplatPakLoupe"

namespace SplatPakLoupe
{
	namespace
	{
		bool bPinned = false;
		float Zoom = 8.0f;
		ESceneCaptureSource Mode = SCS_FinalColorLDR;
		TSharedPtr<SWidget> Overlay;
		TWeakPtr<SLevelViewport> OverlayViewport;
	}

	bool IsEnabled() { return Overlay.IsValid(); }

	void SetEnabled(bool bEnabled)
	{
		if (bEnabled == IsEnabled()) { return; }
		if (!bEnabled)
		{
			Shutdown();
			return;
		}
		FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		TSharedPtr<SLevelViewport> Viewport = LevelEditor.GetFirstActiveLevelViewport();
		if (!Viewport.IsValid()) { return; }
		Overlay = SNew(SBox).HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(0, 0, 12, 36))
			[
				SNew(SSplatPakLoupe)
			];
		Viewport->AddOverlayWidget(Overlay.ToSharedRef());
		OverlayViewport = Viewport;
	}

	void Shutdown()
	{
		if (TSharedPtr<SLevelViewport> Viewport = OverlayViewport.Pin())
		{
			if (Overlay.IsValid()) { Viewport->RemoveOverlayWidget(Overlay.ToSharedRef()); }
		}
		Overlay.Reset();
		OverlayViewport.Reset();
	}

	float GetZoom() { return Zoom; }
	void SetZoom(float NewZoom) { Zoom = FMath::Clamp(NewZoom, 1.0f, 256.0f); }
	ESceneCaptureSource GetMode() { return Mode; }
	void SetMode(ESceneCaptureSource NewMode) { Mode = NewMode; }
	bool IsPinned() { return bPinned; }
	void SetPinned(bool bNewPinned) { bPinned = bNewPinned; }

	const TArray<ESceneCaptureSource>& GetModes()
	{
		static const TArray<ESceneCaptureSource> Modes = { SCS_FinalColorLDR, SCS_BaseColor, SCS_Normal, SCS_DeviceDepth };
		return Modes;
	}

	FText GetModeName(ESceneCaptureSource InMode)
	{
		switch (InMode)
		{
		case SCS_BaseColor:   return LOCTEXT("BaseColour", "Base colour");
		case SCS_Normal:      return LOCTEXT("Normal", "World normal");
		case SCS_DeviceDepth: return LOCTEXT("Depth", "Depth");
		default:              return LOCTEXT("Final", "Final");
		}
	}
}

void SSplatPakLoupe::Construct(const FArguments& InArgs)
{
	Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	Target->RenderTargetFormat = RTF_RGBA8_SRGB;
	Target->ClearColor = FLinearColor::Black;
	Target->InitAutoFormat(Width, Height);
	Target->UpdateResourceImmediate(true);

	Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Capture->TextureTarget = Target;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true; // Temporal effects settle across the inset's own frames

	Brush.SetResourceObject(Target);
	Brush.ImageSize = FVector2D(Width, Height);
	Brush.DrawAs = ESlateBrushDrawType::Image;

	ChildSlot
	[
		SNew(SBorder)
		.Padding(2.0f)
		.BorderBackgroundColor_Lambda([] { return SplatPakLoupe::IsPinned() ? FLinearColor(1.0f, 0.6f, 0.1f) : FLinearColor(0.8f, 0.8f, 0.8f); })
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBox).WidthOverride(Width).HeightOverride(Height)
				[
					SNew(SImage).Image(&Brush)
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(6.0f, 4.0f)
			[
				SNew(STextBlock)
				.ShadowOffset(FVector2D(1, 1))
				.Text_Lambda([]
				{
					return FText::Format(LOCTEXT("Caption", "{0} ×{1}{2}"), SplatPakLoupe::GetModeName(SplatPakLoupe::GetMode()),
						FText::AsNumber(FMath::RoundToInt(SplatPakLoupe::GetZoom())),
						SplatPakLoupe::IsPinned() ? LOCTEXT("Pinned", "  pinned") : FText::GetEmpty());
				})
			]
		]
	];
}

SSplatPakLoupe::~SSplatPakLoupe()
{
	if (Capture && Capture->IsRegistered())
	{
		Capture->UnregisterComponent();
	}
}

void SSplatPakLoupe::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Target);
	Collector.AddReferencedObject(Capture);
}

bool SSplatPakLoupe::UpdateView(FVector& OutEye, FVector& OutDir, FVector& OutUp, float& OutTanHalfFov, UWorld*& OutWorld)
{
	// Play in editor: the player's camera, looking at the centre of the view (the mouse drives the camera there)
	if (GEditor && GEditor->PlayWorld)
	{
		APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController();
		if (!PC || !PC->PlayerCameraManager) { return false; }
		const FRotator Rotation = PC->PlayerCameraManager->GetCameraRotation();
		OutEye = PC->PlayerCameraManager->GetCameraLocation();
		OutDir = Rotation.Vector();
		OutUp = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Z);
		OutTanHalfFov = FMath::Tan(FMath::DegreesToRadians(PC->PlayerCameraManager->GetFOVAngle()) * 0.5f);
		OutWorld = GEditor->PlayWorld;
		return true;
	}

	FLevelEditorViewportClient* Client = GCurrentLevelEditingViewportClient;
	if (!Client || !Client->Viewport || !Client->IsPerspective()) { return false; }
	const FIntPoint Size = Client->Viewport->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0) { return false; }
	const FRotator Rotation = Client->GetViewRotation();
	const FRotationMatrix Axes(Rotation);
	const float TanH = FMath::Tan(FMath::DegreesToRadians(Client->ViewFOV) * 0.5f);
	const float TanV = TanH * (float)Size.Y / (float)Size.X;
	// The mouse's ray: its offset from the centre of the view, as tangents of the horizontal and vertical half-angles
	const float Nx = 2.0f * (float)Client->Viewport->GetMouseX() / (float)Size.X - 1.0f;
	const float Ny = 1.0f - 2.0f * (float)Client->Viewport->GetMouseY() / (float)Size.Y;
	OutEye = Client->GetViewLocation();
	OutDir = (Axes.GetUnitAxis(EAxis::X) + Axes.GetUnitAxis(EAxis::Y) * (Nx * TanH) + Axes.GetUnitAxis(EAxis::Z) * (Ny * TanV)).GetSafeNormal();
	OutUp = Axes.GetUnitAxis(EAxis::Z);
	// The inset shows Width / Zoom of the viewport's pixels across
	OutTanHalfFov = TanH * (float)Width / (float)Size.X;
	OutWorld = Client->GetWorld();
	return OutWorld != nullptr;
}

void SSplatPakLoupe::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	FVector Eye, Dir, Up;
	float TanHalfFov = 0.0f;
	UWorld* World = nullptr;
	if (!UpdateView(Eye, Dir, Up, TanHalfFov, World)) { return; }
	if (SplatPakLoupe::IsPinned()) { Dir = PinnedDir; } else { PinnedDir = Dir; }

	if (CaptureWorld.Get() != World)
	{
		if (Capture->IsRegistered()) { Capture->UnregisterComponent(); }
		Capture->RegisterComponentWithWorld(World);
		CaptureWorld = World;
	}
	Capture->CaptureSource = SplatPakLoupe::GetMode();
	Capture->FOVAngle = FMath::RadiansToDegrees(2.0f * FMath::Atan(TanHalfFov / SplatPakLoupe::GetZoom()));
	Capture->SetWorldLocationAndRotation(Eye, FRotationMatrix::MakeFromXZ(Dir, Up).Rotator());
	Capture->CaptureScene();
}

FReply SSplatPakLoupe::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SplatPakLoupe::SetZoom(SplatPakLoupe::GetZoom() * FMath::Pow(2.0f, 0.25f * MouseEvent.GetWheelDelta()));
	return FReply::Handled();
}

FReply SSplatPakLoupe::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		SplatPakLoupe::SetPinned(!SplatPakLoupe::IsPinned());
		return FReply::Handled();
	}
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const TArray<ESceneCaptureSource>& Modes = SplatPakLoupe::GetModes();
		SplatPakLoupe::SetMode(Modes[(Modes.IndexOfByKey(SplatPakLoupe::GetMode()) + 1) % Modes.Num()]);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
