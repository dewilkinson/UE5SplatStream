// SplatPakEditorModule.cpp
// The editor module: the importers (factories register themselves) and the SplatPak Rendering tab (Tools menu).

#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "SSplatPakLoupe.h"
#include "SSplatPakRenderingPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "SplatPakEditor"

class FSplatPakEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(SSplatPakRenderingPanel::TabName, FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
			{
				return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SSplatPakRenderingPanel)];
			}))
			.SetDisplayName(LOCTEXT("TabTitle", "SplatPak Rendering"))
			.SetTooltipText(LOCTEXT("TabTip", "Splat rendering switches, pass visualizers and the loupe inset"))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

		// The same from the console (and scripts)
		Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("SplatPak.Rendering"), TEXT("Opens the SplatPak Rendering tab"),
			FConsoleCommandDelegate::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(SSplatPakRenderingPanel::TabName); })));
		Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("SplatPak.Loupe"), TEXT("SplatPak.Loupe [0|1] [zoom]: shows or hides the loupe inset"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				SplatPakLoupe::SetEnabled(Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !SplatPakLoupe::IsEnabled());
				if (Args.Num() > 1) { SplatPakLoupe::SetZoom(FCString::Atof(*Args[1])); }
			})));
	}

	virtual void ShutdownModule() override
	{
		for (IConsoleObject* Command : Commands) { IConsoleManager::Get().UnregisterConsoleObject(Command); }
		Commands.Reset();
		SplatPakLoupe::Shutdown();
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SSplatPakRenderingPanel::TabName);
		}
	}

private:
	TArray<IConsoleObject*> Commands;
};

IMPLEMENT_MODULE(FSplatPakEditorModule, SplatPakEditor)

#undef LOCTEXT_NAMESPACE
