// SplatPakRenderingModule.cpp

#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"
#include "SplatViewExtension.h"

class FSplatPakRenderingModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Must happen before the global shader map is built, hence the PostConfigInit loading phase.
		const FString ShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("SplatPak"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/SplatPak"), ShaderDir);

		// View extensions need the engine's renderer, which exists only once the engine is up.
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda([this]()
		{
			ViewExtension = FSceneViewExtensions::NewExtension<FSplatViewExtension>();
		});
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		ViewExtension.Reset();
	}

private:
	TSharedPtr<FSplatViewExtension, ESPMode::ThreadSafe> ViewExtension;
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE(FSplatPakRenderingModule, SplatPakRendering)
