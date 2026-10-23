using System.IO;
using UnrealBuildTool;

// The proprietary bluesec-codec, linked as a prebuilt static library. Stage it with Scripts/BuildCodec.ps1.
public class BluesecCodec : ModuleRules
{
	public BluesecCodec(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string IncludeDir = Path.Combine(ModuleDirectory, "include");
		string Library = Path.Combine(ModuleDirectory, "lib", "Win64", "bluesec-codec.lib");
		if (!File.Exists(Library))
		{
			throw new BuildException("bluesec-codec is not staged. Run Scripts/BuildCodec.ps1 from the project root first (expected " + Library + ").");
		}

		PublicSystemIncludePaths.Add(IncludeDir);
		PublicAdditionalLibraries.Add(Library);
	}
}
