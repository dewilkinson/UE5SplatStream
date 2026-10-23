# Generates project files and builds the editor target (includes MyPlugin and Surfels).
param([string]$Config = "Development")
$ErrorActionPreference = "Stop"

# Surfels links the proprietary codec, staged from the surfels repo (gitignored here).
if (-not (Test-Path "$PSScriptRoot\..\Plugins\Surfels\Source\ThirdParty\BluesecCodec\lib\Win64\bluesec-codec.lib")) {
	& "$PSScriptRoot\BuildCodec.ps1"
	if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

$ue = & "$PSScriptRoot\FindEngine.ps1"
$proj = Resolve-Path "$PSScriptRoot\..\PluginSandbox.uproject"
& "$ue\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="$proj" -game -engine
& "$ue\Engine\Build\BatchFiles\Build.bat" PluginSandboxEditor Win64 $Config -project="$proj" -waitmutex
exit $LASTEXITCODE
