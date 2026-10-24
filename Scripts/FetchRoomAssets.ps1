# Downloads the living-room furniture for the Nanite + splat demo from Poly Haven (CC0: no
# restrictions, no attribution required) as glTF with 1K textures, into ThirdPartyAssets\polyhaven
# (gitignored). Re-running skips models already downloaded.
param(
	[string[]]$Models = @(
		"sofa_03", "mid_century_lounge_chair", "modern_coffee_table_01", "side_table_01",
		"vintage_oil_lamp", "drawer_cabinet", "wooden_bookshelf_worn",
		"brass_vase_01", "potted_plant_02", "Chandelier_01"
	),
	[string[]]$Textures = @("herringbone_parquet", "painted_plaster_wall", "brown_brick_02"),
	[string]$Resolution = "1k"
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$root = Join-Path (Resolve-Path "$PSScriptRoot\..") "ThirdPartyAssets\polyhaven"
New-Item -ItemType Directory -Force $root | Out-Null

foreach ($id in $Models) {
	$dir = Join-Path $root $id
	if (Test-Path (Join-Path $dir "$id.gltf")) { Write-Host "have     $id"; continue }
	$files = Invoke-RestMethod "https://api.polyhaven.com/files/$id" -TimeoutSec 60
	$entry = $files.gltf.$Resolution.gltf
	if (-not $entry) { throw "$id has no $Resolution glTF" }
	New-Item -ItemType Directory -Force $dir | Out-Null
	Invoke-WebRequest $entry.url -OutFile (Join-Path $dir "$id.gltf") -TimeoutSec 300
	# The .bin and textures, at the relative paths the .gltf references.
	foreach ($inc in $entry.include.PSObject.Properties) {
		$dest = Join-Path $dir ($inc.Name -replace "/", "\")
		New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
		Invoke-WebRequest $inc.Value.url -OutFile $dest -TimeoutSec 300
	}
	$mb = [Math]::Round(((Get-ChildItem $dir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), 1)
	Write-Host "fetched  $id ($mb MB)"
}

# Surface textures for the room shell: colour, normal (DirectX convention, as Unreal expects) and roughness.
foreach ($id in $Textures) {
	$dir = Join-Path $root "textures\$id"
	New-Item -ItemType Directory -Force $dir | Out-Null
	$files = $null
	foreach ($map in @{ "Diffuse" = "diff"; "nor_dx" = "nor_dx"; "Rough" = "rough" }.GetEnumerator()) {
		$dest = Join-Path $dir "${id}_$($map.Value).jpg"
		if (Test-Path $dest) { continue }
		if (-not $files) { $files = Invoke-RestMethod "https://api.polyhaven.com/files/$id" -TimeoutSec 60 }
		$entry = $files.($map.Key).$Resolution.jpg
		if (-not $entry) { throw "$id has no $Resolution $($map.Key) map" }
		Invoke-WebRequest $entry.url -OutFile $dest -TimeoutSec 300
	}
	Write-Host "texture  $id"
}
