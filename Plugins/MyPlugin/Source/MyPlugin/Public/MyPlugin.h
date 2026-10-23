#pragma once

#include "Modules/ModuleManager.h"

class FMyPluginModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
