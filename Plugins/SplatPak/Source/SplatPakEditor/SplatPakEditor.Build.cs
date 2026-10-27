using UnrealBuildTool;

// Editor-only: importing .gstx packages as USplatPakAsset.
public class SplatPakEditor : ModuleRules
{
	public SplatPakEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealEd", "SplatPak", "SplatPakRendering",
			"MeshDescription", "StaticMeshDescription", "AssetRegistry", "PhysicsCore" });
	}
}
