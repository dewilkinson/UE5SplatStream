# Builds the Nanite + splat living-room demo level (/Game/Demo/Map_LivingRoom, or /Game/LivingRoom with -Sandbox) in the demo project
# (see FindDemoProject.ps1; -Project overrides it) and writes preview captures to that project's
# Saved\RenderCheck. Downloads the furniture into this repository's ThirdPartyAssets first if needed.
#   -Splat dwarves   splat sample to stand on the coffee table   -Height 35   its height in cm
#   -Emissive 1      its Emissive Intensity (the room uses auto exposure)
#   -Reimport        import the sample packages again over the existing assets (after a package format change)
#   -Sandbox         build into this repository's PluginSandbox under /Game/LivingRoom, the copy committed here
#   -Samples <dir>   the .gstx samples (default: a splatpak or 3DGSStream checkout beside this repository)
param(
	[string]$Samples = "",
	[string]$Splat = "dwarves",
	[string]$Height = "35",
	[string]$Emissive = "1",
	[string]$Project = "",
	[switch]$Reimport,
	[switch]$Sandbox
)
if (-not $Samples) {
	$Samples = @("splatpak", "3DGSStream") | ForEach-Object { Join-Path $PSScriptRoot "..\..\$_\assets" } |
		Where-Object { Test-Path (Join-Path $_ "$Splat\$Splat.gstx") } | Select-Object -First 1
	if (-not $Samples) { throw "No $Splat.gstx found beside this repository; clone https://github.com/dewilkinson/splatpak next to it or pass -Samples." }
}
& "$PSScriptRoot\FetchRoomAssets.ps1"
$ue = & "$PSScriptRoot\FindEngine.ps1"
if ($Sandbox) { $Project = "$PSScriptRoot\..\PluginSandbox.uproject" }
$proj = & "$PSScriptRoot\FindDemoProject.ps1" -Project $Project
$env:ROOM_CONTENT_ROOT = if ($Sandbox) { "/Game/LivingRoom" } else { "/Game/Demo" }
$env:ROOM_ASSETS_DIR = Join-Path (Resolve-Path "$PSScriptRoot\..") "ThirdPartyAssets\polyhaven"
$script = Resolve-Path "$PSScriptRoot\BuildLivingRoom.py"
$env:SPLATPAK_SAMPLES = $Samples
$env:ROOM_SPLAT = $Splat
$env:ROOM_SPLAT_HEIGHT = $Height
$env:ROOM_SPLAT_EMISSIVE = $Emissive
$env:ROOM_REIMPORT = if ($Reimport) { "1" } else { "0" }
# A "py" console command (not -ExecutePythonScript, which quits as soon as the script returns): the
# script waits for shaders and lighting to settle before capturing, then quits the editor itself.
& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$proj" -ExecCmds="py $($script.Path -replace '\\', '/')" `
	-dx12 -RenderOffscreen -unattended -nopause -nosplash -NoLiveCoding -log
exit $LASTEXITCODE
