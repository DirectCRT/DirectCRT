# Prints the system DPI (96 = 100% scaling, 144 = 150%)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Dpi {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  [DllImport("user32.dll")] public static extern uint GetDpiForSystem();
}
"@
[Dpi]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
[Dpi]::GetDpiForSystem()
