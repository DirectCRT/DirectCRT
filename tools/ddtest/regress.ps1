# Runs ddtest's blits pattern, which draws with DirectDraw and GDI and checks
# the result, in every combination of interface version, presentation method
# and bit depth, and prints a table. Exits with 1 if any run failed.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\ddtest\regress.ps1
#   ... -Dll bin\DebugLog\ddraw.dll       another DirectCRT build
#   ... -System                           Windows' own DirectDraw
#   ... -Frames 120 -Size 800x600 -Versions 1,7 -Methods flip,blt -Depths 8,32
#
# The runs use copies of ddtest.exe and the DLL in a new folder under %TEMP%,
# with a default directcrt.ini, so your own settings don't matter. The
# fullscreen runs take over the screen; leave the mouse and keyboard alone. A
# display mode the DLL doesn't offer (Windows' own DirectDraw sometimes refuses
# 8 and 16-bit modes on Windows 10 and 11) is listed as skipped, not failed.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\bin\Release\ddtest.exe"),
    [string]$Dll = (Join-Path $PSScriptRoot "..\..\bin\Release\ddraw.dll"),
    [switch]$System,
    [int]$Frames = 60,
    [string]$Size = "640x480",
    [string]$Versions = "1,2,4,7",
    [string]$Methods = "flip,blt,vblank,lock,windowed",
    [string]$Depths = "8,16,32"
)
$ErrorActionPreference = "Stop"
# Comma-separated: powershell -File passes "1,7" as one string
$versionList = $Versions -split "," | ForEach-Object { $_.Trim() }
$methodList = $Methods -split "," | ForEach-Object { $_.Trim() }
$depthList = $Depths -split "," | ForEach-Object { $_.Trim() }
$Exe = (Resolve-Path $Exe).Path

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("ddtest-regress-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory $work | Out-Null
Copy-Item $Exe $work
if (-not $System) {
    $Dll = (Resolve-Path $Dll).Path
    Copy-Item $Dll (Join-Path $work "ddraw.dll")
    & (Join-Path $PSScriptRoot "..\default-ini.ps1") -Dll (Join-Path $work "ddraw.dll") | Out-Null
}
$test = Join-Path $work (Split-Path $Exe -Leaf)
$label = if ($System) { "Windows' own DirectDraw" } else { $Dll }
Write-Host "ddtest regression: $label, $Size, $Frames frames per run"

$failed = 0; $passed = 0; $skipped = 0
foreach ($v in $versionList) {
    foreach ($m in $methodList) {
        # Windowed runs at the desktop's depth: one is enough
        $runDepths = if ($m -eq "windowed") { @(32) } else { $depthList }
        foreach ($b in $runDepths) {
            $log = Join-Path $work "log.txt"
            if (Test-Path $log) { Remove-Item $log }
            $argv = @("--ddraw", $v, "--present", $m, "--bpp", $b, "--size", $Size,
                      "--pattern", "blits", "--start", "--frames", $Frames, "--log", "`"$log`"")
            $p = Start-Process -FilePath $test -ArgumentList $argv -WorkingDirectory $work -PassThru
            $null = $p.Handle # so ExitCode is kept
            if (-not $p.WaitForExit(60000)) { $p.Kill(); $p.WaitForExit() }
            $text = if (Test-Path $log) { Get-Content $log -Raw } else { "no log written (exit code $($p.ExitCode))" }

            $result = "FAILED"
            if ($p.ExitCode -eq 0 -and $text -match "check: passed") { $result = "passed"; $passed++ }
            elseif ($text -match "SetDisplayMode failed: DDERR_(UNSUPPORTED|INVALIDMODE)") { $result = "skipped"; $skipped++ }
            else { $failed++ }

            $detail = ""
            if ($result -eq "passed" -and $text -match "check: passed, (\d+) frames") { $detail = "$($Matches[1]) frames checked" }
            elseif ($result -ne "passed") {
                $line = ($text -split "`r?`n" | Where-Object { $_ -match "^(check: failed|error:)" } | Select-Object -First 1)
                $detail = if ($line) { $line } else { ($text -split "`r?`n")[0] }
            }
            Write-Host ("  ddraw {0}  {1,-8} {2,2}-bit  {3,-7}  {4}" -f $v, $m, $b, $result, $detail)
        }
    }
}

Remove-Item -Recurse -Force $work
Write-Host "$passed passed, $failed failed, $skipped skipped (mode not offered)"
if ($failed) { exit 1 }
