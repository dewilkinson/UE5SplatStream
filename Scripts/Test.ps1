# Runs MyPlugin automation tests headless and writes a report to Saved\TestReport.
param([string]$Filter = "MyPlugin")
$ue = & "$PSScriptRoot\FindEngine.ps1"
$proj = Resolve-Path "$PSScriptRoot\..\PluginSandbox.uproject"
$report = Join-Path (Split-Path $proj) "Saved\TestReport"
& "$ue\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$proj" -ExecCmds="Automation RunTests $Filter; Quit" `
	-unattended -nopause -nullrhi -nosplash -log -ReportExportPath="$report"
Write-Host "Report: $report\index.json"
exit $LASTEXITCODE
