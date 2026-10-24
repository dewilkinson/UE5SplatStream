# Builds the Nanite + splat living-room demo level (/Game/Demo/Map_LivingRoom) and writes a preview
# capture to Saved\RenderCheck\living_room.png. Downloads the furniture first if needed.
#   -Splat dwarves   splat sample to stand on the coffee table   -Height 35   its height in cm
#   -Emissive 1      its Emissive Intensity (the room uses auto exposure)
param(
	[string]$Samples = "C:\github\surfels\assets",
	[string]$Splat = "dwarves",
	[string]$Height = "35",
	[string]$Emissive = "1"
)
& "$PSScriptRoot\FetchRoomAssets.ps1"
$ue = & "$PSScriptRoot\FindEngine.ps1"
$proj = Resolve-Path "$PSScriptRoot\..\PluginSandbox.uproject"
$script = Resolve-Path "$PSScriptRoot\BuildLivingRoom.py"
$env:SURFELS_SAMPLES = $Samples
$env:ROOM_SPLAT = $Splat
$env:ROOM_SPLAT_HEIGHT = $Height
$env:ROOM_SPLAT_EMISSIVE = $Emissive
# A "py" console command (not -ExecutePythonScript, which quits as soon as the script returns): the
# script waits for shaders and lighting to settle before capturing, then quits the editor itself.
& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$proj" -ExecCmds="py $($script.Path -replace '\\', '/')" `
	-dx12 -RenderOffscreen -unattended -nopause -nosplash -NoLiveCoding -log
exit $LASTEXITCODE
