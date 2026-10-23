# Returns the root of the newest installed UE5 engine (or $env:UE_ROOT if set).
if ($env:UE_ROOT) { return $env:UE_ROOT }
$dat = "$env:ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat"
if (-not (Test-Path $dat)) { throw "No Epic launcher install list found. Install UE5 from the Epic Games Launcher or set UE_ROOT." }
$eng = (Get-Content $dat -Raw | ConvertFrom-Json).InstallationList |
	Where-Object { $_.AppName -match '^UE_5' } |
	Sort-Object { [version]($_.AppName -replace '^UE_', '') } -Descending |
	Select-Object -First 1
if (-not $eng) { throw "No UE5 install found. Install UE5 from the Epic Games Launcher or set UE_ROOT." }
$eng.InstallLocation
