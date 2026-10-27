# Returns the project the demo scripts work in: the private UE5SplatPakDemo project next to this
# repository (..\..\UE5SplatPakDemo) when it exists, otherwise PluginSandbox itself. Showcase content
# (Fab packs, demo scenes) belongs in UE5SplatPakDemo so it never ends up in this public repository.
param([string]$Project = "")
if ($Project) { return (Resolve-Path $Project).Path }
$demo = Join-Path $PSScriptRoot "..\..\UE5SplatPakDemo\UE5SplatPakDemo.uproject"
if (Test-Path $demo) { return (Resolve-Path $demo).Path }
return (Resolve-Path "$PSScriptRoot\..\PluginSandbox.uproject").Path
