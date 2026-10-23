# Runs the plugins' automation tests headless and writes a report to Saved\TestReport.
# -Filter takes Unreal's syntax: test name prefixes joined with '+', e.g. "Surfels.Package".
param([string]$Filter = "MyPlugin+Surfels")
$ue = & "$PSScriptRoot\FindEngine.ps1"
$proj = Resolve-Path "$PSScriptRoot\..\PluginSandbox.uproject"
$report = Join-Path (Split-Path $proj) "Saved\TestReport"
& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$proj" -ExecCmds="Automation RunTests $Filter; Quit" `
	-unattended -nopause -nullrhi -nosplash -log -ReportExportPath="$report"
Write-Host "Report: $report\index.json"
exit $LASTEXITCODE
