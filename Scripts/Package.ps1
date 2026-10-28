# Packages the SplatPak plugin as a standalone, distributable plugin into ..\Packaged\SplatPak.
$ue = & "$PSScriptRoot\FindEngine.ps1"
$plugin = Resolve-Path "$PSScriptRoot\..\Plugins\SplatPak\SplatPak.uplugin"
$out = Join-Path (Resolve-Path "$PSScriptRoot\..") "Packaged\SplatPak"
& "$ue\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="$plugin" -Package="$out" -TargetPlatforms=Win64 -Rocket
exit $LASTEXITCODE
