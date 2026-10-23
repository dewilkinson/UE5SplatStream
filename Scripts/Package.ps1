# Packages UE5Splats as a standalone, distributable plugin into ..\Packaged\UE5Splats.
$ue = & "$PSScriptRoot\FindEngine.ps1"
$plugin = Resolve-Path "$PSScriptRoot\..\Plugins\UE5Splats\UE5Splats.uplugin"
$out = Join-Path (Resolve-Path "$PSScriptRoot\..") "Packaged\UE5Splats"
& "$ue\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="$plugin" -Package="$out" -TargetPlatforms=Win64 -Rocket
exit $LASTEXITCODE
