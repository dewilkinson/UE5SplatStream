# Renders the sample scans in the editor (D3D12, off-screen) and writes captures to Saved\RenderCheck:
# <sample>_mesh.png, <sample>_fallback.png and <sample>_hidden.png (baseline). The editor log says
# which path actually drew ("LogSurfelsRendering: Drawing surfels with ...").
# -Warp renders on Microsoft's software rasterizer, which supports mesh shaders, for GPUs that don't.
# The first run compiles Unreal's shaders and can take a long time.
param(
	[string]$Samples = "C:\github\surfels\assets",
	[switch]$Warp
)
$ue = & "$PSScriptRoot\FindEngine.ps1"
$root = Resolve-Path "$PSScriptRoot\.."
$proj = Join-Path $root "PluginSandbox.uproject"
$script = Resolve-Path "$PSScriptRoot\RenderCheck.py"

# Each run re-imports into a clean folder and creates fresh test levels.
Remove-Item (Join-Path $root "Content\RenderCheck") -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $root "Saved\RenderCheck") -Recurse -Force -ErrorAction SilentlyContinue

$env:SURFELS_SAMPLES = $Samples
$extra = @()
if ($Warp) { $extra += "-warp" }
& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$proj" -ExecutePythonScript="$script" `
	-dx12 -RenderOffscreen -unattended -nopause -nosplash -NoLiveCoding -log @extra
exit $LASTEXITCODE
