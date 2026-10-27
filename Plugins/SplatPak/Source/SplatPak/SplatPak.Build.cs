using UnrealBuildTool;

// The UObject side: the imported .gstx asset, the component that places it and its scene proxy.
public class SplatPak : ModuleRules
{
	public SplatPak(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "PhysicsCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI", "SplatPakRendering", "GSTx" }); // GSTx: the streaming runtime

		// The editor follows the active level viewport (USplatPakComponent::FindView)
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
