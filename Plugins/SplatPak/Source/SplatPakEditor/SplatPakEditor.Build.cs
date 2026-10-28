using UnrealBuildTool;

// Editor-only: importing .gstx packages as USplatPakAsset and SoundStage specs, and the SplatPak Rendering tab.
public class SplatPakEditor : ModuleRules
{
	public SplatPakEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealEd", "SplatPak", "SplatPakRendering",
			"MeshDescription", "StaticMeshDescription", "AssetRegistry", "PhysicsCore",
			"Slate", "SlateCore", "InputCore", "LevelEditor", "WorkspaceMenuStructure" }); // The SplatPak Rendering tab and the loupe
	}
}
