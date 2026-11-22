# Starts a program, waits, captures the screen (in physical pixels) to a PNG and stops
# the program. Default: the primary monitor. -Rect "x,y,w,h": that area of the
# virtual screen. -Client: the client area of the program's window of class -Class
# (ddtest's display window by default), raised above other windows first.
# -Cursor "x,y" puts the mouse there before capturing.
param(
    [string]$Exe,
    [string]$ArgList,
    [string]$Out,
    [int]$Delay = 5,
    [string]$Rect = "",
    [switch]$Client,
    [string]$Class = "DDTestDisplay",
    [string]$Cursor = ""
)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Cap {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
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
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
[Cap]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

$p = Start-Process -FilePath $Exe -ArgumentList $ArgList -PassThru -WorkingDirectory (Split-Path $Exe)
Start-Sleep $Delay
if ($Cursor) {
    $c = $Cursor.Split(",")
    [Cap]::SetCursorPos([int]$c[0], [int]$c[1]) | Out-Null
    Start-Sleep 1
}

if ($Client) {
    $h = [Cap]::FindForPid([uint32]$p.Id, $Class)
    if ($h -eq [IntPtr]::Zero) { Write-Host "window not found"; if (-not $p.HasExited) { Stop-Process -Id $p.Id }; exit 1 }
    # On top of everything (HWND_TOPMOST, no move, no size, no activation), then let it redraw
    [Cap]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0013) | Out-Null
    Start-Sleep 2
    $r = New-Object Cap+RECT
    [Cap]::GetClientRect($h, [ref]$r) | Out-Null
    $pt = New-Object Cap+POINT
    [Cap]::ClientToScreen($h, [ref]$pt) | Out-Null
    $x = $pt.X; $y = $pt.Y; $w = $r.R; $hh = $r.B
} elseif ($Rect) {
    $v = $Rect.Split(","); $x = [int]$v[0]; $y = [int]$v[1]; $w = [int]$v[2]; $hh = [int]$v[3]
} else {
    $x = 0; $y = 0; $w = [Cap]::GetSystemMetrics(0); $hh = [Cap]::GetSystemMetrics(1)
}

$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size $w, $hh))
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host ("captured {0}x{1} at {2},{3} -> {4}" -f $w, $hh, $x, $y, $Out)

if (-not $p.HasExited) { Stop-Process -Id $p.Id }
