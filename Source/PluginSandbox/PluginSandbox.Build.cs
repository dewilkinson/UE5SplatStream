using UnrealBuildTool;

public class PluginSandbox : ModuleRules
{
	public PluginSandbox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "MyPlugin" });
	}
}
