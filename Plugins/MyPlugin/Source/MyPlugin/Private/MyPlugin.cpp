#include "MyPlugin.h"

DEFINE_LOG_CATEGORY_STATIC(LogMyPlugin, Log, All);

void FMyPluginModule::StartupModule()
{
	UE_LOG(LogMyPlugin, Log, TEXT("MyPlugin started"));
}

void FMyPluginModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FMyPluginModule, MyPlugin)
