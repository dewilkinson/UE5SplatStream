# Stages SplatPak as a Fab code plugin in ..\Fab, for review only: nothing here uploads or submits anything.
# The folder is committed (binaries through Git LFS) while this repository is private: it holds the
# proprietary GSTx library, so it must leave the repository before the repository is ever made public.
# Layout per Fab's code plugin requirements: the .uplugin, Source (the GSTx codec as a ThirdParty module, headers
# and prebuilt static library), Shaders, Config and Content, plus the licences; no Binaries or Intermediate, since
# Epic builds the binaries. The plugin is built first with BuildPlugin, so a broken plugin is never staged.
param(
	[string]$CodecLicense = "C:\github\3DGSStream\scripts\public-release-stubs\libs\gstx\prebuilt\LICENSE.txt"
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..").Path
$plugin = Join-Path $root "Plugins\SplatPak"

if (-not (Test-Path "$plugin\Source\ThirdParty\GSTx\lib\Win64\gstx.lib")) {
	& "$PSScriptRoot\BuildCodec.ps1"
	if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
& "$PSScriptRoot\Package.ps1"
if ($LASTEXITCODE) { throw "BuildPlugin failed; nothing staged." }

$version = (Get-Content "$plugin\SplatPak.uplugin" -Raw | ConvertFrom-Json).VersionName
$engine = (Split-Path (& "$PSScriptRoot\FindEngine.ps1") -Leaf) -replace '^UE_', ''
$fab = Join-Path $root "Fab"
$stage = Join-Path $fab "SplatPak"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item "$plugin\SplatPak.uplugin", "$plugin\README.md" $stage
robocopy "$plugin\Shaders" "$stage\Shaders" /E /NFL /NDL /NJH /NJS | Out-Null
robocopy "$plugin\Source" "$stage\Source" /E /XD Intermediate /NFL /NDL /NJH /NJS | Out-Null
robocopy "$plugin\Config" "$stage\Config" /E /NFL /NDL /NJH /NJS | Out-Null
if (Test-Path "$plugin\Resources") { robocopy "$plugin\Resources" "$stage\Resources" /E /NFL /NDL /NJH /NJS | Out-Null }
New-Item -ItemType Directory -Force "$stage\Content" | Out-Null
Set-Content "$stage\Content\README.txt" "SplatPak ships no content; import .gstx packages into your own project."
Copy-Item "$root\LICENSE" "$stage\LICENSE.txt"
Copy-Item $CodecLicense "$stage\LICENSE-GSTx-Binary.txt"

$zip = Join-Path $fab "SplatPak-$version-UE$engine.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Staged $stage and $zip (not uploaded)."
exit 0 # robocopy's "files copied" code (1) would otherwise leak out as this script's exit code
