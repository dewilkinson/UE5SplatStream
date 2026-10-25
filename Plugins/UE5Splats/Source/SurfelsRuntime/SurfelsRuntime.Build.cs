using UnrealBuildTool;

// The UObject side: the imported .gstx asset, the component that places it and its scene proxy.
public class SurfelsRuntime : ModuleRules
{
	public SurfelsRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI", "SurfelsRendering" });
	}
}
