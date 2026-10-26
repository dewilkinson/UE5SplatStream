using System.IO;
using UnrealBuildTool;

// The proprietary GSTx codec library, linked as a prebuilt static library. Stage it with Scripts/BuildCodec.ps1.
public class GSTx : ModuleRules
{
	public GSTx(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string IncludeDir = Path.Combine(ModuleDirectory, "include");
		string Library = Path.Combine(ModuleDirectory, "lib", "Win64", "gstx.lib");
		if (!File.Exists(Library))
		{
			throw new BuildException("The GSTx codec library is not staged. Run Scripts/BuildCodec.ps1 from the project root first (expected " + Library + ").");
		}

		PublicSystemIncludePaths.Add(IncludeDir);
		PublicAdditionalLibraries.Add(Library);
	}
}
