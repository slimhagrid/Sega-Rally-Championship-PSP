# M8 regression: record every scenario of tools/menu_scenarios_rec.txt on the SH-2 oracle (Windows Python) and run the C port against it in WSL
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$wslroot = (wsl -d Ubuntu -- wslpath -a ($root -replace "\\", "/")).Trim()
$fail = 0
foreach ($line in Get-Content "tools/menu_scenarios_rec.txt") {
    $n = ($line -split '#')[0].Trim()
    if ($n -eq '') { continue }
    python tools/menu_oracle.py $n | Out-Null
    $r = wsl -d Ubuntu -- bash -c "cd '$wslroot'; bash tools/test_menu.sh $n 2>&1 | tail -4"
    $ok = ($r -join "`n") -match "identical"
    if ($ok -and ($r -join "`n") -notmatch "FAILED|DIFFERS") { Write-Host "ok   $n" } else { Write-Host "FAIL $n"; $r; $fail++ }
}
exit $fail
