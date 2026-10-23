using UnrealBuildTool;

public class PluginSandboxTarget : TargetRules
{
	public PluginSandboxTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("PluginSandbox");
	}
}
