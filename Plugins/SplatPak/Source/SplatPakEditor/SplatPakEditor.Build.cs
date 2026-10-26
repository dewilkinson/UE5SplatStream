using UnrealBuildTool;

// Editor-only: importing .gstx packages as USplatAsset.
public class SplatPakEditor : ModuleRules
{
	public SplatPakEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealEd", "SplatPak" });
	}
}
