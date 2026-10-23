# Generates project files and builds the editor target (includes MyPlugin).
param([string]$Config = "Development")
$ErrorActionPreference = "Stop"
$ue = & "$PSScriptRoot\FindEngine.ps1"
$proj = Resolve-Path "$PSScriptRoot\..\PluginSandbox.uproject"
& "$ue\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="$proj" -game -engine
& "$ue\Engine\Build\BatchFiles\Build.bat" PluginSandboxEditor Win64 $Config -project="$proj" -waitmutex
exit $LASTEXITCODE
