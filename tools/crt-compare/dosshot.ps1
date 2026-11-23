# Starts DOSBox Staging with a config from dosconf.py, waits until SHOW.COM has
# drawn its picture, takes DOSBox's rendered screenshot (the picture after the
# shader, exactly as DOSBox shows it, without a screen capture) and saves it
# to -Out, then shuts DOSBox down.
#
# Uses DOSBox's web server (dosconf.py enables it on 127.0.0.1:-Port): SHOW.COM
# writes 'DCRT' to 0040:00F0 once the picture is up, read through the memory
# API; the screenshot is the "rendshot" hotkey, Alt+F5, the only way to ask
# for one, so the DOSBox window is brought to the front first; the shutdown is
# the API's.
param(
    [string]$Exe,
    [string]$ArgList,
    [string]$Out,
    [string]$CaptureDir,
    [int]$Port = 8086,
    [int]$Timeout = 60,
    [int]$Settle = 3
)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Dos {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
  public static IntPtr FindForPid(uint pid, string cls) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint wp; GetWindowThreadProcessId(h, out wp);
      var sb = new System.Text.StringBuilder(256); GetClassName(h, sb, 256);
      if (wp == pid && IsWindowVisible(h) && sb.ToString() == cls) { found = h; return false; }
      return true; }, IntPtr.Zero);
    return found;
  }
  public static void Key(byte vk, bool up) {
    keybd_event(vk, (byte)MapVirtualKey(vk, 0), up ? 2u : 0u, UIntPtr.Zero);
  }
}
"@

$api = "http://127.0.0.1:$Port/api/v1"
$VK_MENU = 0x12
$VK_F5 = 0x74

function Stop-Dosbox($p) {
    try { Invoke-WebRequest -UseBasicParsing -Method Post -Uri "$api/dosbox/shutdown" -TimeoutSec 5 | Out-Null } catch { }
    if (-not $p.WaitForExit(10000)) { Stop-Process -Id $p.Id -Force }
}

function Fail($p, $msg) {
    Write-Host $msg
    Stop-Dosbox $p
    exit 1
}

New-Item -ItemType Directory -Force -Path $CaptureDir | Out-Null
Get-ChildItem -Path $CaptureDir -Filter *.png | Remove-Item

$p = Start-Process -FilePath $Exe -ArgumentList $ArgList -PassThru -WorkingDirectory (Split-Path $Exe)

# The picture is up when SHOW.COM's marker is
$deadline = (Get-Date).AddSeconds($Timeout)
$ready = $false
while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
    try {
        $r = Invoke-WebRequest -UseBasicParsing -Uri "$api/memory/0x4F0/4" -TimeoutSec 2
        if ([System.Text.Encoding]::ASCII.GetString($r.Content) -eq "DCRT") { $ready = $true; break }
    } catch { }
    Start-Sleep -Milliseconds 250
}
if (-not $ready) { Fail $p "DOSBox: the picture didn't come up" }

# A few frames for crt-auto's shader switch and the fullscreen switch
Start-Sleep $Settle

$h = [IntPtr]::Zero
while ((Get-Date) -lt $deadline -and $h -eq [IntPtr]::Zero) {
    $h = [Dos]::FindForPid([uint32]$p.Id, "SDL_app")
    if ($h -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 250 }
}
if ($h -eq [IntPtr]::Zero) { Fail $p "DOSBox: window not found" }

# Windows lets a program bring another to the front after a key press
[Dos]::Key($VK_MENU, $false); [Dos]::Key($VK_MENU, $true)
[Dos]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
if ([Dos]::GetForegroundWindow() -ne $h) { Fail $p "DOSBox: couldn't bring its window to the front" }

# Alt+F5: the rendered screenshot
[Dos]::Key($VK_MENU, $false); Start-Sleep -Milliseconds 50
[Dos]::Key($VK_F5, $false); Start-Sleep -Milliseconds 50
[Dos]::Key($VK_F5, $true); Start-Sleep -Milliseconds 50
[Dos]::Key($VK_MENU, $true)

# Wait for the file, until its size stops changing
$shot = $null
$last = -1
while ((Get-Date) -lt $deadline) {
    $f = Get-ChildItem -Path $CaptureDir -Filter *-rendered.png | Select-Object -First 1
    if ($f) {
        if ($f.Length -gt 0 -and $f.Length -eq $last) { $shot = $f; break }
        $last = $f.Length
    }
    Start-Sleep -Milliseconds 250
}
if (-not $shot) { Fail $p "DOSBox: no rendered screenshot in $CaptureDir" }

Copy-Item -Path $shot.FullName -Destination $Out -Force
Write-Host ("captured {0} -> {1}" -f $shot.Name, $Out)

Stop-Dosbox $p
