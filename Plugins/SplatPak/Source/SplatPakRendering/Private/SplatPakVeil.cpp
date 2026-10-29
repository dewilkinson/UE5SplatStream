// SplatPakVeil.cpp

#include "SplatPakVeil.h"

#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "SceneRendering.h"
#include "ShaderParameterStruct.h"

static TAutoConsoleVariable<int32> CVarSplatPakVeil(
	TEXT("r.SplatPak.Veil"),
	1,
	TEXT("1 = blur the view while a scan initialises and its view's finest levels arrive (USplatPakComponent::bLoadingVeil; default).\n")
	TEXT("0 = never."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarSplatPakVeilSize(
	TEXT("r.SplatPak.Veil.Size"),
	0.02f,
	TEXT("The loading veil's full blur, a Gaussian sigma as a share of the view's height (2%: SplatPak uses 0.8%, but the coarse first levels still showed as dark blotches through that)."),
	ECVF_RenderThreadSafe);

class FSplatPakVeilPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplatPakVeilPS);
	SHADER_USE_PARAMETER_STRUCT(FSplatPakVeilPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER(FIntPoint, ViewMin)
		SHADER_PARAMETER(FIntPoint, ViewMax)
		SHADER_PARAMETER(FIntPoint, BlurStep)
		SHADER_PARAMETER(int32, Radius)
		SHADER_PARAMETER(float, Sigma)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSplatPakVeilPS, "/Plugin/SplatPak/Private/SplatPakVeil.usf", "MainPS", SF_Pixel);

namespace SplatPakVeil
{
	namespace
	{
		FCriticalSection Lock;
		TMap<const FSceneInterface*, TMap<const void*, float>> Strengths;
	}

	void SetStrength(const FSceneInterface* Scene, const void* Source, float Strength)
	{
		if (!Scene) { return; }
		FScopeLock Guard(&Lock);
		if (Strength > 0.0f)
		{
			Strengths.FindOrAdd(Scene).Add(Source, FMath::Min(Strength, 1.0f));
		}
		else if (TMap<const void*, float>* Sources = Strengths.Find(Scene))
		{
			Sources->Remove(Source);
			if (Sources->IsEmpty()) { Strengths.Remove(Scene); }
		}
	}

	float GetStrength(const FSceneInterface* Scene)
	{
		FScopeLock Guard(&Lock);
		float Strongest = 0.0f;
		if (const TMap<const void*, float>* Sources = Strengths.Find(Scene))
		{
			for (const TPair<const void*, float>& Source : *Sources) { Strongest = FMath::Max(Strongest, Source.Value); }
		}
		return Strongest;
	}

	void AddPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTexture* SceneColor)
	{
		if (!SceneColor || CVarSplatPakVeil.GetValueOnRenderThread() == 0 || !View.Family || SceneColor->Desc.NumSamples > 1) { return; }
		const float Strength = GetStrength(View.Family->Scene);
		const FIntRect Rect = View.ViewRect;
		const float Sigma = Strength * FMath::Max(0.0f, CVarSplatPakVeilSize.GetValueOnRenderThread()) * (float)Rect.Height();
		RDG_EVENT_SCOPE(GraphBuilder, "SplatPak loading veil");
		AddBlur(GraphBuilder, View, SceneColor, Sigma);
	}

	void AddBlur(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTexture* Texture, float Sigma)
	{
		const FIntRect Rect = View.ViewRect;
		if (!Texture || Sigma < 0.3f || Rect.Area() <= 0) { return; }
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Texture->Desc.Extent, Texture->Desc.Format, FClearValueBinding::Black,
			TexCreate_RenderTargetable | TexCreate_ShaderResource);
		FRDGTextureRef Across = GraphBuilder.CreateTexture(Desc, TEXT("SplatPak.BlurAcross"));
		TShaderMapRef<FSplatPakVeilPS> PixelShader(View.ShaderMap);

		auto Blur = [&](FRDGTextureRef Input, FRDGTextureRef Output, FIntPoint Step, const TCHAR* Name)
		{
			FSplatPakVeilPS::FParameters* Parameters = GraphBuilder.AllocParameters<FSplatPakVeilPS::FParameters>();
			Parameters->InputTexture = Input;
			Parameters->ViewMin = Rect.Min;
			Parameters->ViewMax = Rect.Max;
			Parameters->BlurStep = Step;
			Parameters->Radius = FMath::Min(FMath::CeilToInt(3.0f * Sigma), 96);
			Parameters->Sigma = Sigma;
			Parameters->RenderTargets[0] = FRenderTargetBinding(Output, Output == Texture ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::ENoAction);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("%s", Name), PixelShader, Parameters, Rect);
		};
		Blur(Texture, Across, FIntPoint(1, 0), TEXT("Across"));
		Blur(Across, Texture, FIntPoint(0, 1), TEXT("Down"));
	}
}
