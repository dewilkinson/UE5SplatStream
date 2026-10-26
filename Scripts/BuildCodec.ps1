# Stages the GSTx codec static library and headers for the UE5Splats plugin.
#
# -CodecRepo points at a checkout that contains libs/gstx:
#   - the public mirror (https://github.com/dewilkinson/splatlab, cloned with Git LFS): its
#     prebuilt Release library is copied as is;
#   - the private 3DGSStream repo: the library is built from source with CMake.
# The staged files are gitignored and never committed here: the codec is not covered by this
# repository's licence. Unreal links the release CRT (/MD) in every configuration, so only a
# Release library is staged.
param(
	[string]$CodecRepo = "C:\github\3DGSStream"
)
$ErrorActionPreference = "Stop"
$src = Join-Path $CodecRepo "libs\gstx"
if (-not (Test-Path "$src\CMakeLists.txt")) { throw "The GSTx codec library was not found at $src. Pass -CodecRepo <path to a splatlab or 3DGSStream checkout>." }

$dest = Resolve-Path "$PSScriptRoot\..\Plugins\SplatPak\Source\ThirdParty\GSTx"
New-Item -ItemType Directory -Force "$dest\include", "$dest\lib\Win64" | Out-Null

$prebuilt = Join-Path $src "prebuilt\Release\gstx.lib"
if ((Test-Path $prebuilt) -and (Get-Item $prebuilt).Length -ge 4096) {
	# A real library, not an unpulled Git LFS pointer (those are ~130 bytes).
	Copy-Item $prebuilt "$dest\lib\Win64" -Force
	Write-Host "Using the prebuilt codec from $prebuilt"
}
else {
	if (Test-Path $prebuilt) { Write-Warning "$prebuilt is a Git LFS pointer; run 'git lfs pull' there to use the real codec. Building from source instead." }
	$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
	$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
	$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
	if (-not (Test-Path $cmake)) { $cmake = "cmake" }

	$build = Join-Path $dest "Intermediate"
	& $cmake -S $src -B $build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL" "-DGSTX_USE_PREBUILT=OFF"
	if ($LASTEXITCODE) { exit $LASTEXITCODE }
	& $cmake --build $build --config Release
	if ($LASTEXITCODE) { exit $LASTEXITCODE }
	Copy-Item "$build\Release\gstx.lib" "$dest\lib\Win64" -Force
}

Copy-Item "$src\*.h" "$dest\include" -Force
Write-Host "Staged the GSTx codec library into $dest"
