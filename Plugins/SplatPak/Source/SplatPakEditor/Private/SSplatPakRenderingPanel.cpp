// SSplatPakRenderingPanel.cpp

#include "SSplatPakRenderingPanel.h"

#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "LevelEditorViewport.h"
#include "SoundStageActor.h"
#include "SplatPakComponent.h"
#include "SSplatPakLoupe.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SplatPakRendering"

const FName SSplatPakRenderingPanel::TabName(TEXT("SplatPakRendering"));

namespace
{
	// Every splat component in an open world (not the class defaults or Blueprint templates)
	TArray<USplatPakComponent*> Splats()
	{
		TArray<USplatPakComponent*> Out;
		for (TObjectIterator<USplatPakComponent> It; It; ++It)
		{
			if (IsValid(*It) && !It->IsTemplate() && It->GetWorld() && It->IsRegistered()) { Out.Add(*It); }
		}
		return Out;
	}

	ECheckBoxState State(bool b) { return b ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }

	// ---- Console variables ----

	IConsoleVariable* CVar(const TCHAR* Name) { return IConsoleManager::Get().FindConsoleVariable(Name); }

	TSharedRef<SWidget> CVarCheck(const FText& Label, const FText& Tip, const TCHAR* Name, int32 On = 1, int32 Off = 0)
	{
		return SNew(SCheckBox)
			.ToolTipText(Tip)
			.IsChecked_Lambda([Name, On] { IConsoleVariable* V = CVar(Name); return State(V && V->GetInt() == On); })
			.OnCheckStateChanged_Lambda([Name, On, Off](ECheckBoxState S) { if (IConsoleVariable* V = CVar(Name)) { V->Set(S == ECheckBoxState::Checked ? On : Off, ECVF_SetByConsole); } })
			[SNew(STextBlock).Text(Label)];
	}

	// ---- Splat component properties, set on every splat through PostEditChangeProperty (which updates the proxy) ----

	template <typename PropertyType, typename ValueType>
	void SetOnAll(FName Name, ValueType Value)
	{
		PropertyType* Property = FindFProperty<PropertyType>(USplatPakComponent::StaticClass(), Name);
		if (!Property) { return; }
		for (USplatPakComponent* C : Splats())
		{
			Property->SetPropertyValue_InContainer(C, Value);
			FPropertyChangedEvent Event(Property);
			C->PostEditChangeProperty(Event);
		}
	}

	template <typename PropertyType, typename ValueType>
	ValueType GetFirst(FName Name, ValueType Default)
	{
		PropertyType* Property = FindFProperty<PropertyType>(USplatPakComponent::StaticClass(), Name);
		TArray<USplatPakComponent*> All = Splats();
		return (Property && All.Num()) ? Property->GetPropertyValue_InContainer(All[0]) : Default;
	}

	TSharedRef<SWidget> SplatCheck(const FText& Label, const FText& Tip, FName Name)
	{
		return SNew(SCheckBox)
			.ToolTipText(Tip)
			.IsChecked_Lambda([Name] { return State(GetFirst<FBoolProperty, bool>(Name, false)); })
			.OnCheckStateChanged_Lambda([Name](ECheckBoxState S) { SetOnAll<FBoolProperty, bool>(Name, S == ECheckBoxState::Checked); })
			[SNew(STextBlock).Text(Label)];
	}

	TSharedRef<SWidget> SplatSlider(const FText& Label, const FText& Tip, FName Name, float Min, float Max)
	{
		return SNew(SHorizontalBox).ToolTipText(Tip)
			+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Label)]
			+ SHorizontalBox::Slot().FillWidth(0.55f)
			[
				SNew(SSpinBox<float>).MinValue(Min).MaxValue(Max).MinSliderValue(Min).MaxSliderValue(Max)
				.Value_Lambda([Name] { return GetFirst<FFloatProperty, float>(Name, 0.0f); })
				.OnValueChanged_Lambda([Name](float V) { SetOnAll<FFloatProperty, float>(Name, V); })
			];
	}

	// ---- The level viewport's view mode: the GBuffer views show what the lit pass wrote ----

	void SetBufferView(FName Buffer)
	{
		if (FLevelEditorViewportClient* Client = GCurrentLevelEditingViewportClient)
		{
			if (Buffer.IsNone())
			{
				Client->SetViewMode(VMI_Lit);
			}
			else
			{
				Client->SetViewMode(VMI_VisualizeBuffer);
				Client->ChangeBufferVisualizationMode(Buffer);
			}
			Client->Invalidate();
		}
	}

	bool IsBufferView(FName Buffer)
	{
		const FLevelEditorViewportClient* Client = GCurrentLevelEditingViewportClient;
		if (!Client) { return false; }
		const bool bVisualizing = Client->GetViewMode() == VMI_VisualizeBuffer;
		return Buffer.IsNone() ? !bVisualizing : (bVisualizing && Client->IsBufferVisualizationModeSelected(Buffer));
	}

	TSharedRef<SWidget> Section(const FText& Title, TSharedRef<SWidget> Body, bool bOpen = true)
	{
		return SNew(SExpandableArea)
			.InitiallyCollapsed(!bOpen)
			.AreaTitle(Title)
			.Padding(FMargin(8, 4))
			.BodyContent()[Body];
	}

	TSharedRef<SVerticalBox> Rows(std::initializer_list<TSharedRef<SWidget>> Widgets)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		for (const TSharedRef<SWidget>& W : Widgets) { Box->AddSlot().AutoHeight().Padding(0, 2)[W]; }
		return Box;
	}
}

void SSplatPakRenderingPanel::Construct(const FArguments& InArgs)
{
	// The pass visualizers: one radio row per view of the level viewport
	TSharedRef<SVerticalBox> BufferViews = SNew(SVerticalBox);
	const TPair<FName, FText> Buffers[] = {
		{ NAME_None, LOCTEXT("ViewFinal", "Final image") },
		{ TEXT("BaseColor"), LOCTEXT("ViewBase", "Base colour (lit splats' albedo)") },
		{ TEXT("WorldNormal"), LOCTEXT("ViewNormal", "World normal (lit splats, after smoothing)") },
		{ TEXT("Roughness"), LOCTEXT("ViewRough", "Roughness") },
		{ TEXT("SceneDepth"), LOCTEXT("ViewDepth", "Scene depth") },
	};
	for (const TPair<FName, FText>& B : Buffers)
	{
		const FName Buffer = B.Key;
		BufferViews->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SCheckBox).Style(FAppStyle::Get(), "RadioButton")
			.IsChecked_Lambda([Buffer] { return State(IsBufferView(Buffer)); })
			.OnCheckStateChanged_Lambda([Buffer](ECheckBoxState) { SetBufferView(Buffer); })
			[SNew(STextBlock).Text(B.Value)]
		];
	}

	TSharedRef<SVerticalBox> LoupeModes = SNew(SVerticalBox);
	for (ESceneCaptureSource Mode : SplatPakLoupe::GetModes())
	{
		LoupeModes->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SCheckBox).Style(FAppStyle::Get(), "RadioButton")
			.IsChecked_Lambda([Mode] { return State(SplatPakLoupe::GetMode() == Mode); })
			.OnCheckStateChanged_Lambda([Mode](ECheckBoxState) { SplatPakLoupe::SetMode(Mode); })
			[SNew(STextBlock).Text(SplatPakLoupe::GetModeName(Mode))]
		];
	}

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(4)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text_Lambda([] { return FText::Format(LOCTEXT("Count", "{0} splat components in the open worlds"), FText::AsNumber(Splats().Num())); })
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[
				Section(LOCTEXT("Passes", "Passes"), Rows({
					CVarCheck(LOCTEXT("PassGaussians", "Gaussians"), LOCTEXT("PassGaussiansTip", "Sorted, blended Gaussians for streamed splats; off draws them as discs (r.SplatPak.Pass.Gaussians)"), TEXT("r.SplatPak.Pass.Gaussians")),
					CVarCheck(LOCTEXT("PassDiscs", "Discs"), LOCTEXT("PassDiscsTip", "The disc pass (r.SplatPak.Pass.Discs)"), TEXT("r.SplatPak.Pass.Discs")),
					CVarCheck(LOCTEXT("PassFill", "Gap fill"), LOCTEXT("PassFillTip", "The occluder and shadow mesh drawn under the splats (r.SplatPak.Pass.Fill)"), TEXT("r.SplatPak.Pass.Fill")),
					CVarCheck(LOCTEXT("PassLit", "Lit GBuffer pass"), LOCTEXT("PassLitTip", "Lit scans written into the GBuffer; off draws them unlit (r.SplatPak.Pass.Lit)"), TEXT("r.SplatPak.Pass.Lit")),
					CVarCheck(LOCTEXT("MeshShaders", "Mesh shaders"), LOCTEXT("MeshShadersTip", "Amplification and mesh shaders; off uses the vertex-shader fallback (r.SplatPak.MeshShaders)"), TEXT("r.SplatPak.MeshShaders")),
					CVarCheck(LOCTEXT("Occlusion", "Occlusion culling"), LOCTEXT("OcclusionTip", "Skip meshlets behind the occluder mesh (r.SplatPak.OcclusionCulling)"), TEXT("r.SplatPak.OcclusionCulling")),
					CVarCheck(LOCTEXT("Veil", "Loading veil"), LOCTEXT("VeilTip", "Blur the view while a scan initialises and its view's two finest levels arrive, in play (r.SplatPak.Veil)"), TEXT("r.SplatPak.Veil")),
					CVarCheck(LOCTEXT("ColourMatch", "Colour match"), LOCTEXT("ColourMatchTip", "Each object's colours graded toward the scene around it; off, as captured (r.SplatPak.ColourMatch)"), TEXT("r.SplatPak.ColourMatch")),
					CVarCheck(LOCTEXT("DropShadows", "Drop shadows"), LOCTEXT("DropShadowsTip", "The objects' shadows drawn on their floors (r.SplatPak.DropShadow)"), TEXT("r.SplatPak.DropShadow")),
				}))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[
				Section(LOCTEXT("Visualize", "Pass visualizers"), Rows({
					BufferViews,
					CVarCheck(LOCTEXT("Culled", "Culled meshlets only"), LOCTEXT("CulledTip", "Draw only the meshlets occlusion culling skips (r.SplatPak.OcclusionCulling 2)"), TEXT("r.SplatPak.OcclusionCulling"), 2, 1),
					SplatCheck(LOCTEXT("Glow", "Refinement glow"), LOCTEXT("GlowTip", "Blocks glow as they refine to a finer level"), GET_MEMBER_NAME_CHECKED(USplatPakComponent, bRefinementVisualizer)),
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("ShadowMeshTip", "Show the shadow mesh, normally hidden (it casts the splats' shadows, feeds Lumen, collides and fills gaps)"))
					.IsChecked_Lambda([] { TArray<USplatPakComponent*> All = Splats(); UStaticMeshComponent* M = All.Num() ? All[0]->GetShadowMeshComponent() : nullptr; return State(M && M->IsVisible()); })
					.OnCheckStateChanged_Lambda([](ECheckBoxState S)
					{
						for (USplatPakComponent* C : Splats())
						{
							if (UStaticMeshComponent* M = C->GetShadowMeshComponent()) { M->SetVisibility(S == ECheckBoxState::Checked); M->SetHiddenInGame(S != ECheckBoxState::Checked); }
						}
					})
					[SNew(STextBlock).Text(LOCTEXT("ShadowMesh", "Shadow mesh"))],
				}))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[
				Section(LOCTEXT("Splats", "Splats"), Rows({
					SplatCheck(LOCTEXT("Gaussians", "Gaussians (per splat)"), LOCTEXT("GaussiansTip", "Each component's Gaussians setting"), GET_MEMBER_NAME_CHECKED(USplatPakComponent, bGaussians)),
					SplatCheck(LOCTEXT("Dither", "Dithered transitions"), LOCTEXT("DitherTip", "Cross-fade between levels"), GET_MEMBER_NAME_CHECKED(USplatPakComponent, bDitheredTransitions)),
					SplatCheck(LOCTEXT("Fill", "Shadow mesh gap fill"), LOCTEXT("FillTip", "Each component's gap fill"), GET_MEMBER_NAME_CHECKED(USplatPakComponent, bShadowMeshFill)),
					SplatCheck(LOCTEXT("Lit", "Lit"), LOCTEXT("LitTip", "Shade the splats with Unreal's lights (G-buffer)"), GET_MEMBER_NAME_CHECKED(USplatPakComponent, bLit)),
					SplatSlider(LOCTEXT("LitBlend", "Lit blend"), LOCTEXT("LitBlendTip", "How much of the baked colour becomes albedo"), GET_MEMBER_NAME_CHECKED(USplatPakComponent, LitBlend), 0.0f, 1.0f),
					SplatSlider(LOCTEXT("Smoothing", "Normal smoothing"), LOCTEXT("SmoothingTip", "Lean lit normals toward the shadow mesh's"), GET_MEMBER_NAME_CHECKED(USplatPakComponent, NormalSmoothing), 0.0f, 1.0f),
					SplatSlider(LOCTEXT("SplatScale", "Splat scale"), LOCTEXT("SplatScaleTip", "Disc size"), GET_MEMBER_NAME_CHECKED(USplatPakComponent, SplatScale), 0.1f, 4.0f),
					SplatSlider(LOCTEXT("Emissive", "Emissive intensity"), LOCTEXT("EmissiveTip", "Brightness of the baked colour"), GET_MEMBER_NAME_CHECKED(USplatPakComponent, EmissiveIntensity), 0.0f, 4.0f),
					SNew(SUniformGridPanel).SlotPadding(2)
					+ SUniformGridPanel::Slot(0, 0)[SNew(SButton).Text(LOCTEXT("Restream", "Restream")).ToolTipText(LOCTEXT("RestreamTip", "Stream every splat again from its two coarsest levels"))
						.OnClicked_Lambda([] { for (USplatPakComponent* C : Splats()) { C->Restream(); } return FReply::Handled(); })]
					+ SUniformGridPanel::Slot(1, 0)[SNew(SButton).Text(LOCTEXT("Evict", "Evict")).ToolTipText(LOCTEXT("EvictTip", "Drop everything but the two coarsest levels"))
						.OnClicked_Lambda([] { for (USplatPakComponent* C : Splats()) { C->Evict(); } return FReply::Handled(); })],
				}))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[
				Section(LOCTEXT("Loupe", "Loupe inset"), Rows({
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("LoupeTip", "What is under the mouse, drawn again magnified in the viewport's corner. Wheel: zoom; left click: next view; right click: pin"))
					.IsChecked_Lambda([] { return State(SplatPakLoupe::IsEnabled()); })
					.OnCheckStateChanged_Lambda([](ECheckBoxState S) { SplatPakLoupe::SetEnabled(S == ECheckBoxState::Checked); })
					[SNew(STextBlock).Text(LOCTEXT("LoupeOn", "Show the loupe"))],
					SNew(SCheckBox)
					.IsChecked_Lambda([] { return State(SplatPakLoupe::IsPinned()); })
					.OnCheckStateChanged_Lambda([](ECheckBoxState S) { SplatPakLoupe::SetPinned(S == ECheckBoxState::Checked); })
					[SNew(STextBlock).Text(LOCTEXT("Pin", "Pinned"))],
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("Zoom", "Zoom"))]
					+ SHorizontalBox::Slot().FillWidth(0.55f)
					[
						SNew(SSpinBox<float>).MinValue(1.0f).MaxValue(256.0f).MinSliderValue(1.0f).MaxSliderValue(64.0f).SliderExponent(2.0f)
						.Value_Lambda([] { return SplatPakLoupe::GetZoom(); })
						.OnValueChanged_Lambda([](float V) { SplatPakLoupe::SetZoom(V); })
					],
					LoupeModes,
				}))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[
				Section(LOCTEXT("Stage", "SoundStage"), Rows({
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("VolumesTip", "The stages' volumes as wireframes, in the editor and in play"))
					.IsChecked_Lambda([]
					{
						for (TObjectIterator<ASoundStageActor> It; It; ++It) { if (It->GetWorld() && !It->IsTemplate()) { return State(It->bShowVolumes); } }
						return ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([](ECheckBoxState S)
					{
						for (TObjectIterator<ASoundStageActor> It; It; ++It)
						{
							if (!It->GetWorld() || It->IsTemplate()) { continue; }
							It->bShowVolumesInGame = S == ECheckBoxState::Checked;
							It->SetVolumesVisible(S == ECheckBoxState::Checked);
						}
					})
					[SNew(STextBlock).Text(LOCTEXT("Volumes", "Show volumes"))],
				}))
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
