# Builds the bluesec-codec static library from the surfels repo and stages it for the Surfels plugin.
# The codec is proprietary: the staged headers and .lib are gitignored and never committed here.
# Unreal links the release CRT (/MD) in every configuration, so only a Release build is staged.
param([string]$SurfelsRepo = "C:\github\surfels")
$ErrorActionPreference = "Stop"
$src = Join-Path $SurfelsRepo "libs\bluesec-codec"
if (-not (Test-Path "$src\CMakeLists.txt")) { throw "bluesec-codec not found at $src" }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) { $cmake = "cmake" }

$dest = Resolve-Path "$PSScriptRoot\..\Plugins\Surfels\Source\ThirdParty\BluesecCodec"
$build = Join-Path $dest "Intermediate"
& $cmake -S $src -B $build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $cmake --build $build --config Release
if ($LASTEXITCODE) { exit $LASTEXITCODE }

New-Item -ItemType Directory -Force "$dest\include", "$dest\lib\Win64" | Out-Null
Copy-Item "$src\*.h" "$dest\include" -Force
Copy-Item "$build\Release\bluesec-codec.lib" "$dest\lib\Win64" -Force
Write-Host "Staged bluesec-codec into $dest"
