# M2: build the Ghidra project for 0RALLY.DAT (SH-2 big endian, loaded at 0x06004000) with the symbols from RALLY.DBG.
#   python tools/dbg_parse.py            (first: writes extracted/analysis/symbols.tsv + modules.tsv)
#   powershell -File tools/ghidra_import.ps1 [-Open]
# Project: ghidra/SegaRally.gpr (git-ignored). Re-running re-imports from scratch (-overwrite).
param([switch]$Open)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $env:JAVA_HOME) { throw "set JAVA_HOME to a JDK 21" }
$env:PATH = "$env:JAVA_HOME\bin;$env:PATH"
if (-not $env:GHIDRA_HOME) { throw "set GHIDRA_HOME to the Ghidra folder (12.1.x)" }
$ghidra = $env:GHIDRA_HOME
$proj = Join-Path $root "ghidra"
New-Item -ItemType Directory -Force $proj | Out-Null

if ($Open) {
    & "$ghidra\ghidraRun.bat"
    return
}
& "$ghidra\support\analyzeHeadless.bat" $proj SegaRally `
    -import (Join-Path $root "extracted\disc\files\0RALLY.DAT") `
    -overwrite `
    -processor "SuperH:BE:32:SH-2" `
    -loader BinaryLoader -loader-baseAddr 0x06004000 `
    -scriptPath (Join-Path $root "tools\ghidra") `
    -preScript SetupRally.java $root `
    -postScript ReportRally.java $root
