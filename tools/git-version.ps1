# Writes inc/git.h for res.rc and the debug log: the commit, the branch and
# the version, from the last version tag. The build runs it before compiling.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\git-version.ps1 [-Git <git.exe>] [-Out <file>]
#
# Version tags are vX.Y.Z. On a tag, the version is X.Y.Z; N commits after
# it, the version is X.Y.Z.N and its string X.Y.Z-N-g<commit>, with -dirty
# when there are changes. cnc-ddraw's four-part tags (v7.1.0.0) don't count.
# With no tag, the version is 0.0.0.N, N counting every commit.

param(
    [string]$Git = "git",
    [string]$Out = ""
)

if (-not $Out) { $Out = Join-Path $PSScriptRoot "..\inc\git.h" }

function Invoke-Git([string[]]$GitArgs)
{
    try
    {
        $o = & $Git @GitArgs 2>$null
        if ($LASTEXITCODE -eq 0 -and $o) { return ([string]@($o)[0]).Trim() }
    }
    catch {}
    return $null
}

$commit = Invoke-Git @("describe", "--match=NeVeRmAtCh", "--always", "--dirty")
$branch = Invoke-Git @("rev-parse", "--abbrev-ref", "HEAD")
$described = Invoke-Git @("describe", "--tags", "--match=v[0-9]*", "--exclude=v*.*.*.*", "--dirty")

$v = @(0, 0, 0, 0)
if ($described -match '^v(\d+)\.(\d+)\.(\d+)(?:-(\d+)-g[0-9a-f]+)?(?:-dirty)?$')
{
    $v = @([int]$Matches[1], [int]$Matches[2], [int]$Matches[3], [int]$Matches[4])
    $string = $described.Substring(1)
}
elseif ($commit)
{
    if ($described) { Write-Warning "git-version: tag '$described' isn't vX.Y.Z; using 0.0.0" }
    $count = Invoke-Git @("rev-list", "--count", "HEAD")
    if ($count) { $v[3] = [int]$count }
    $string = "0.0.0-$($v[3])-g$commit"
}
else
{
    $string = "UNKNOWN"
}

# Each part of a Windows version is 16 bits
$v = $v | ForEach-Object { [Math]::Min($_, 65535) }

if (-not $commit) { $commit = "UNKNOWN" }
if (-not $branch) { $branch = "UNKNOWN" }

$text = @(
    "#ifndef GIT_H"
    "#define GIT_H"
    ""
    "/* Written by tools/git-version.ps1 */"
    "#define GIT_COMMIT `"$commit`""
    "#define GIT_BRANCH `"$branch`""
    "#define GIT_VERSION_MAJOR    $($v[0])"
    "#define GIT_VERSION_MINOR    $($v[1])"
    "#define GIT_VERSION_BUILD    $($v[2])"
    "#define GIT_VERSION_REVISION $($v[3])"
    "#define GIT_VERSION_STRING   `"$string`""
    ""
    "#endif"
    ""
) -join "`n"

# Only a change rewrites it, so an unchanged version rebuilds nothing
$old = $null
if (Test-Path $Out) { $old = [IO.File]::ReadAllText($Out) }
if ($old -ne $text) { [IO.File]::WriteAllText($Out, $text) }

Write-Host "git-version: $string ($branch)"
