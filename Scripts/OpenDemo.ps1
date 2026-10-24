# Opens the editor for looking around a scan, in the demo project (see FindDemoProject.ps1;
# -Project overrides it).
#   -Map /Game/Demo/Map_LivingRoom   open a saved level (e.g. from BuildLivingRoom.ps1) at its DemoCamera
# Without -Map: a sample scan placed in a lit blank level (nothing is saved):
#   -Sample dwarves   sample folder under -Samples    -Level 0   LOD level (-1 = coarsest)
#   -Emissive 2       Emissive Intensity
param(
	[string]$Map = "",
	[string]$Samples = "C:\github\surfels\assets",
	[string]$Sample = "dwarves",
	[int]$Level = 0,
	[string]$Emissive = "2",
	[string]$Project = ""
)
$ue = & "$PSScriptRoot\FindEngine.ps1"
$proj = & "$PSScriptRoot\FindDemoProject.ps1" -Project $Project
if ($Map) {
	$script = (Resolve-Path "$PSScriptRoot\FocusDemoCamera.py").Path -replace '\\', '/'
	$arguments = "`"$proj`" $Map -ExecCmds=`"py $script`""
}
else {
	$script = (Resolve-Path "$PSScriptRoot\OpenDemo.py").Path -replace '\\', '/'
	$env:SURFELS_SAMPLES = $Samples
	$env:DEMO_SAMPLE = $Sample
	$env:DEMO_LEVEL = "$Level"
	$env:DEMO_EMISSIVE = $Emissive
	# -ExecutePythonScript would quit the editor once the script ends; a "py" console command keeps it open.
	$arguments = "`"$proj`" -ExecCmds=`"py $script`""
}
Start-Process -FilePath "$ue\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList $arguments
