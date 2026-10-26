param([string]$Script = "", [int]$Timeout = 300, [string]$Menu = "0", [switch]$KeepSaves, [switch]$Snd, [switch]$Trace, [string]$Patch = "", [string]$Keys = "", [string]$RacePad = "")
# stage dist/SegaRallyPSP on the PPSSPP stick, put the script in as data/autoplay.txt, start PPSSPP, take the screenshots the game asks for (data/shot.req), wait for the END line of data/autoplay.log (or the timeout), close it
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$ppsspp = if ($env:PPSSPP_EXE) { $env:PPSSPP_EXE } else { "PPSSPPWindows64.exe" }          # set PPSSPP_EXE to the full path if it is not on the PATH
$game = "SegaRallyPSP"
$stick = Join-Path $env:USERPROFILE "Documents\PPSSPP\PSP\GAME\$game"
New-Item -ItemType Directory -Force $stick | Out-Null
$xf = @("keycfg.txt", "sndlog", "sndlog.txt", "nocd", "cdvol", "sndvol")
if ($KeepSaves) { $xf += @("BUP0.BIN", "BUP1.BIN") }
robocopy "$root\dist\$game" $stick /MIR /XF @xf /XD shots /NFL /NDL /NJH /NJS /NP | Out-Null
$d = "$stick\data"
Set-Content -Path "$d\menu.txt" -Value $Menu -Encoding ascii
foreach ($f in "shot.req", "play.txt", "cam.txt", "menu_pad.txt", "race_pad.txt", "perf.txt", "menu_unported.txt", "menu_race.txt", "autoplay.log", "menu_patch.txt", "dbglog", "race_trace", "race_trace.txt", "snddump", "cdda.txt", "snd_dump.raw", "snd_stats.txt") { if (Test-Path "$d\$f") { Remove-Item "$d\$f" } }
if (Test-Path "$d\shots") { Remove-Item "$d\shots" -Recurse -Force }
if (-not $KeepSaves) { foreach ($f in "BUP0.BIN", "BUP1.BIN") { foreach ($q in "$stick\$f", "$d\$f") { if (Test-Path $q) { Remove-Item $q } } } }
if ($Patch) { Copy-Item $Patch "$d\menu_patch.txt" }
Copy-Item $Script "$d\autoplay.txt"
if ($RacePad) { Set-Content -Path ($d + [char]92 + "race_pad.txt") -Value ($RacePad -split ";") -Encoding ascii }
if ($Trace) { Set-Content -Path "$d\race_trace" -Value "1" -Encoding ascii }
if ($Snd) { Set-Content -Path "$d\snddump" -Value "1" -Encoding ascii }
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public class WinCap3 {
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
 [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
 public static void Key(byte vk, bool down) { keybd_event(vk, (byte)MapVirtualKey(vk, 0), down ? 0u : 2u, UIntPtr.Zero); }
 public struct RECT { public int L,T,R,B; }
 public struct POINT { public int X,Y; } }
'@
$ksteps = @(); if ($Keys) { $ksteps = $Keys -split ";" }
$ki = 0; $kuntil = $null
New-Item -ItemType Directory -Force "$d\shots" | Out-Null
$p = Start-Process $ppsspp -ArgumentList "`"$stick\EBOOT.PBP`"" -PassThru
$t0 = Get-Date
$ended = $false
$topmost = $false
while (((Get-Date) - $t0).TotalSeconds -lt $Timeout) {
    Start-Sleep -Milliseconds 40
    if ($p.HasExited) { "PPSSPP exited"; break }
    if ($ki -lt $ksteps.Count) {                       # -Keys "waitlog:REGEX;down:z;sleep:500;up:z": real key events for the game's real pad path (Z = cross, X = circle, A = square, S = triangle, space = start, arrows)
        $st = $ksteps[$ki]; $kind, $arg = $st -split ":", 2
        if ($kind -eq "waitlog") { try { if ((Get-Content "$d\autoplay.log" -Raw) -match $arg) { $ki++ } } catch {} }
        elseif ($kind -eq "sleep") { if (-not $kuntil) { $kuntil = (Get-Date).AddMilliseconds([int]$arg) } elseif ((Get-Date) -ge $kuntil) { $kuntil = $null; $ki++ } }
        else {
            $p.Refresh(); [WinCap3]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
            $vk = switch ($arg) { "z" { 0x5A } "x" { 0x58 } "a" { 0x41 } "s" { 0x53 } "space" { 0x20 } "up" { 0x26 } "down" { 0x28 } "left" { 0x25 } "right" { 0x27 } "q" { 0x51 } "w" { 0x57 } "v" { 0x56 } "tab" { 0x09 } }
            [WinCap3]::Key([byte]$vk, ($kind -eq "down")); $ki++
        }
    }
    if (Test-Path "$d\shot.req") {
        try { $nm = (Get-Content "$d\shot.req" -TotalCount 1).Trim() } catch { continue }
        if (-not $nm) { continue }
        $p.Refresh()
        if (-not $topmost) { [WinCap3]::SetWindowPos($p.MainWindowHandle, [IntPtr](-1), 0, 0, 0, 0, 0x0043) | Out-Null; $topmost = $true }
        Start-Sleep -Milliseconds 120
        $r = New-Object WinCap3+RECT
        [WinCap3]::GetClientRect($p.MainWindowHandle, [ref]$r) | Out-Null
        $pt = New-Object WinCap3+POINT
        [WinCap3]::ClientToScreen($p.MainWindowHandle, [ref]$pt) | Out-Null
        $w = $r.R - $r.L; $h = $r.B - $r.T
        $bmp = New-Object System.Drawing.Bitmap $w, $h
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, (New-Object System.Drawing.Size $w, $h))
        $bmp.Save("$d\shots\$nm.png")
        $g.Dispose(); $bmp.Dispose()
        Remove-Item "$d\shot.req" -ErrorAction SilentlyContinue
        continue
    }
    if (Test-Path "$d\autoplay.log") {
        try { $txt = Get-Content "$d\autoplay.log" -Raw } catch { continue }
        if ($txt -cmatch "vec \w+\s+END") { $ended = $true; break }
    }
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
"ended=$ended after $([int]((Get-Date) - $t0).TotalSeconds) s"
if (Test-Path "$d\menu_unported.txt") { "unported:"; Get-Content "$d\menu_unported.txt" }
