# Stage dist/SegaRallyPSP into your PPSSPP memory stick (Documents\PPSSPP\PSP\GAME\SegaRallyPSP), start the EBOOT in PPSSPP (PPSSPP_EXE = the full path of PPSSPPWindows64.exe if it is not on the PATH),
# wait, save a screenshot of the PPSSPP *client area* (no title bar / menu), close it.
#   powershell -File tools/run_ppsspp.ps1 [-Seconds 12] [-Shot out.png] [-Frame desert_p330] [-Play "course 0;replay desert_drive.inp;hold 329"]
# -Play writes data/play.txt (lines separated by ;) = M6 play mode instead of the frame viewer; without it a leftover play.txt is removed.
# -Frame writes data/cam.txt = the name of the first frame to show (M5 frame viewer); without it a leftover cam.txt is removed.
# The PNG has the size of the client area (PPSSPP scales the 480x272 screen); tools/compare_frames.py resizes it back.
param([int]$Seconds = 12, [string]$Shot = "", [string]$Frame = "", [string]$Cam = "", [string]$Play = "", [string]$Pad = "", [string]$Dist = "", [string]$StickName = "")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$ppsspp = if ($env:PPSSPP_EXE) { $env:PPSSPP_EXE } else { "PPSSPPWindows64.exe" }          # set PPSSPP_EXE to the full path if it is not on the PATH
$game = "SegaRallyPSP"          # staged under this name (the EBOOT reads ms0:/PSP/GAME/SegaRallyPSP/data)
if (-not $Dist) { $Dist = "dist\$game" }
if (-not $StickName) { $StickName = $game }
$dist = Join-Path $root $Dist
$stick = Join-Path $env:USERPROFILE "Documents\PPSSPP\PSP\GAME\$StickName"
New-Item -ItemType Directory -Force $stick | Out-Null
robocopy $dist $stick /MIR /XF BUP0.BIN BUP1.BIN keycfg.txt /NFL /NDL /NJH /NJS /NP | Out-Null          # the saves and settings on the stick survive a re-stage
$camfile = Join-Path $stick "data\cam.txt"
$playfile = Join-Path $stick "data\play.txt"
if ($Play) { Set-Content -Path $playfile -Value ($Play -split ";") -Encoding ascii } elseif (Test-Path $playfile) { Remove-Item $playfile }
$padfile = Join-Path $stick "data\menu_pad.txt"          # -Pad "120 2000;124 0;...": the menus' pad script (tick, Saturn pad bits in hex; data/menu_pad.txt); menu_perf.txt then shows the picture times of the run
if ($Pad) { Set-Content -Path $padfile -Value ($Pad -split ";") -Encoding ascii } elseif (Test-Path $padfile) { Remove-Item $padfile }
if ($Frame) { Set-Content -Path $camfile -Value $Frame -Encoding ascii } elseif ($Cam) { Set-Content -Path $camfile -Value $Cam -Encoding ascii } elseif (Test-Path $camfile) { Remove-Item $camfile }
$p = Start-Process $ppsspp -ArgumentList "`"$stick\EBOOT.PBP`"" -PassThru
Start-Sleep -Seconds $Seconds
if ($Shot) {
    Add-Type -AssemblyName System.Drawing
    Add-Type @'
using System; using System.Runtime.InteropServices;
public class WinCap {
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
 public struct RECT { public int L,T,R,B; }
 public struct POINT { public int X,Y; } }
'@
    $p.Refresh()
    [WinCap]::SetWindowPos($p.MainWindowHandle, [IntPtr](-1), 0, 0, 0, 0, 0x0043) | Out-Null   # topmost, no move / resize
    [WinCap]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 800
    $r = New-Object WinCap+RECT
    [WinCap]::GetClientRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $pt = New-Object WinCap+POINT
    [WinCap]::ClientToScreen($p.MainWindowHandle, [ref]$pt) | Out-Null
    $w = $r.R - $r.L; $h = $r.B - $r.T
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $bmp.Save($Shot)
    "client ${w}x${h}: $($p.MainWindowTitle)"
}
Stop-Process -Id $p.Id -Force
