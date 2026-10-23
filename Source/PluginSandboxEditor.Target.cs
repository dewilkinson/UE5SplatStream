using UnrealBuildTool;

public class PluginSandboxEditorTarget : TargetRules
{
	public PluginSandboxEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("PluginSandbox");
	}
}
