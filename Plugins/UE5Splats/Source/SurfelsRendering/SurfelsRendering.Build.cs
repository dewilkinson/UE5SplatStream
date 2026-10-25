using System.IO;
using UnrealBuildTool;

// Everything that must exist before the global shader map is built: shaders, the draw passes,
// the scene view extension and the .gstx format core. Loads at PostConfigInit; defines no UObjects.
public class SurfelsRendering : ModuleRules
{
	public SurfelsRendering(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "RenderCore", "RHI" });
		PrivateDependencyModuleNames.AddRange(new string[] { "CoreUObject", "Engine", "Renderer", "Projects", "GSTx" });

		// FViewInfo and FPostProcessingInputs live in the renderer's private and internal headers.
		string RendererDir = Path.Combine(EngineDirectory, "Source", "Runtime", "Renderer");
		PrivateIncludePaths.Add(Path.Combine(RendererDir, "Private"));
		if (Directory.Exists(Path.Combine(RendererDir, "Internal")))
		{
			PrivateIncludePaths.Add(Path.Combine(RendererDir, "Internal"));
		}
	}
}
