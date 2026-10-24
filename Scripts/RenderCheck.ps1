# Renders the sample scans in the editor (D3D12, off-screen) and writes captures to Saved\RenderCheck:
# <sample>_mesh.png, <sample>_fallback.png and <sample>_hidden.png (baseline), plus
# <sample>_emissive<N>.png for each -Emissive intensity. The editor log says which path actually
# drew ("LogSurfelsRendering: Drawing surfels with ...").
#   -Names dwarves,venus   samples to render (folders under -Samples)
#   -Level -1              LOD level kept resident: -1 = coarsest, 0 = finest
#   -Resolution 1280x720   capture size
#   -Emissive "1,4,16"     extra captures at these emissive intensities (quoted, comma-separated)
#   -NoOccluder            don't place the occluding test cube
#   -Warp                  render on Microsoft's software rasterizer, which supports mesh shaders
# The first run compiles Unreal's shaders and can take a long time.
param(
	[string]$Samples = "C:\github\surfels\assets",
	[string[]]$Names = @("dwarves", "venus"),
	[int]$Level = -1,
	[string]$Resolution = "1280x720",
	[string]$Emissive = "",
	[switch]$NoOccluder,
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
$env:RENDERCHECK_SAMPLES = ($Names -join ",")
$env:RENDERCHECK_LEVEL = "$Level"
$env:RENDERCHECK_RES = $Resolution
$env:RENDERCHECK_EMISSIVE = ($Emissive -replace '\s', '')
$env:RENDERCHECK_OCCLUDER = $(if ($NoOccluder) { "0" } else { "1" })

$extra = @()
if ($Warp) { $extra += "-warp" }
& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$proj" -ExecutePythonScript="$script" `
	-dx12 -RenderOffscreen -unattended -nopause -nosplash -NoLiveCoding -log @extra
exit $LASTEXITCODE
