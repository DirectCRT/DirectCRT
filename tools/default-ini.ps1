# Writes a fresh default directcrt.ini: deletes the one next to the DLL (or
# -Out) and loads the DLL with DIRECTCRT_CONFIG_INIT set, which makes it only
# read its settings, writing the default file when there's none. No game runs.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\default-ini.ps1
#   ... -Dll bin\DebugLog\ddraw.dll -Out C:\somewhere\directcrt.ini
#
# The DLL is 32-bit, so this runs itself again in 32-bit PowerShell if needed.
param(
    [string]$Dll = (Join-Path $PSScriptRoot "..\bin\Release\ddraw.dll"),
    [string]$Out = ""
)
$ErrorActionPreference = "Stop"
$Dll = (Resolve-Path $Dll).Path
if (-not $Out) { $Out = Join-Path (Split-Path $Dll) "directcrt.ini" }
$Out = [System.IO.Path]::GetFullPath($Out)

if ([IntPtr]::Size -ne 4) {
    $ps32 = Join-Path $env:SystemRoot "SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
    & $ps32 -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Dll $Dll -Out $Out
    exit $LASTEXITCODE
}

Add-Type @"
using System; using System.Runtime.InteropServices;
public class DefaultIni {
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern IntPtr LoadLibraryW(string path);
  [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr h);
}
"@

if (Test-Path $Out) { Remove-Item $Out }
$env:DIRECTCRT_CONFIG_INIT = "1"
$env:DIRECTCRT_CONFIG_FILE = $Out
$h = [DefaultIni]::LoadLibraryW($Dll)
if ($h -eq [IntPtr]::Zero) { Write-Host "Couldn't load $Dll"; exit 1 }
[DefaultIni]::FreeLibrary($h) | Out-Null
if (-not (Test-Path $Out)) { Write-Host "$Out wasn't written"; exit 1 }
Write-Host ("Wrote {0} ({1} bytes)" -f $Out, (Get-Item $Out).Length)
