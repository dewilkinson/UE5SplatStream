# Packages MyPlugin as a standalone, distributable plugin into ..\Packaged\MyPlugin.
$ue = & "$PSScriptRoot\FindEngine.ps1"
$plugin = Resolve-Path "$PSScriptRoot\..\Plugins\MyPlugin\MyPlugin.uplugin"
$out = Join-Path (Resolve-Path "$PSScriptRoot\..") "Packaged\MyPlugin"
& "$ue\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="$plugin" -Package="$out" -TargetPlatforms=Win64 -Rocket
exit $LASTEXITCODE
