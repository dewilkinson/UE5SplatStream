using UnrealBuildTool;

// Editor-only: importing .sflw packages as USurfelAsset.
public class SurfelsEditor : ModuleRules
{
	public SurfelsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealEd", "SurfelsRuntime" });
	}
}
