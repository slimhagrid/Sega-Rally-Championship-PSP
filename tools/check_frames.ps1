# M5: run every test frame in PPSSPP and compare with the reference made from the original code.
#   powershell -File tools/check_frames.ps1 [-Only name] [-Suffix ""] [-Index index.txt | index_obj.txt (the object gallery of tools/export_objects.py)]
# Needs dist/ built (tools/build.sh) and the references (tools/export_frames.py).  Writes extracted/ref/shot_<name>.png and cmp_<name>.png.
param([string[]]$Only = @(), [string]$Suffix = "", [string]$Index = "index.txt")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$names = Get-Content "extracted/psp/frames/$Index" | Where-Object { $_ }
foreach ($n in $names) {
    if ($Only.Count -gt 0 -and $Only -notcontains $n) { continue }
    $shot = "extracted/ref/shot_$n.png"
    powershell -NoProfile -File tools/run_ppsspp.ps1 -Seconds 9 -Shot $shot -Frame $n | Out-Null
    $ref = "extracted/ref/$n$Suffix.png"
    $out = python -I tools/compare_frames.py $ref $shot "extracted/ref/cmp_$n.png"
    "{0,-14} {1}" -f $n, (($out | Select-Object -First 1) + " | " + ($out | Select-Object -Skip 1 -First 1))
}
