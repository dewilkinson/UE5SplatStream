// SurfelsRenderingModule.cpp

#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"
#include "SurfelViewExtension.h"

class FSurfelsRenderingModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Must happen before the global shader map is built, hence the PostConfigInit loading phase.
		const FString ShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("Surfels"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/Surfels"), ShaderDir);

		// View extensions need the engine's renderer, which exists only once the engine is up.
		PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddLambda([this]()
		{
			ViewExtension = FSceneViewExtensions::NewExtension<FSurfelViewExtension>();
		});
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		ViewExtension.Reset();
	}

private:
	TSharedPtr<FSurfelViewExtension, ESPMode::ThreadSafe> ViewExtension;
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE(FSurfelsRenderingModule, SurfelsRendering)
