# Run a script from tools/ghidra/ against the saved project (no re-analysis, project untouched).
#   powershell -File tools/ghidra_run.ps1 ExportCallGraph.java
#   powershell -File tools/ghidra_run.ps1 DecompRally.java mainv_loop main_init     (extra args are passed after the project root)
param([Parameter(Mandatory = $true)][string]$Script, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $env:JAVA_HOME) { throw "set JAVA_HOME to a JDK 21" }
if (-not $env:GHIDRA_HOME) { throw "set GHIDRA_HOME to the Ghidra folder (12.1.x)" }
$env:PATH = "$env:JAVA_HOME\bin;$env:PATH"
& (Join-Path $env:GHIDRA_HOME "support\analyzeHeadless.bat") (Join-Path $root "ghidra") SegaRally `
    -process 0RALLY.DAT -noanalysis -readOnly `
    -scriptPath (Join-Path $root "tools\ghidra") `
    -postScript $Script $root @Rest 2>&1 | Select-String -Pattern "\.java>|ERROR|Exception"
