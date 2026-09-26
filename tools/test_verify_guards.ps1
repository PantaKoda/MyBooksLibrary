<#
.SYNOPSIS
    Regression tests for the -Clean guard in scripts/verify.ps1.

.DESCRIPTION
    Copies verify.ps1 into a disposable fake repository under the temporary
    folder, surrounds it with sentinel files, and runs it with
    -Clean -ValidateOnly against safe and unsafe build folders. Nothing outside
    that temporary folder is touched, even if the guard were broken. Exits 0
    only if every unsafe folder is refused with its sentinels intact and every
    legitimate build folder is cleaned.
#>
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$pwsh = (Get-Process -Id $PID).Path
$root = Join-Path ([IO.Path]::GetTempPath()) "mbl-verify-guards-$([guid]::NewGuid().ToString('N'))"
$repo = Join-Path $root 'repo'
$sdk = Join-Path $root 'sdk'
$qt = Join-Path $root 'qt'
$failures = [System.Collections.Generic.List[string]]::new()

function New-Sentinel([string]$Dir) {
    New-Item -ItemType Directory -Force -Path $Dir | Out-Null
    Set-Content -LiteralPath (Join-Path $Dir 'sentinel.txt') -Value 'keep'
}

function New-CMakeCache([string]$Dir, [string]$Source) {
    New-Sentinel $Dir
    Set-Content -LiteralPath (Join-Path $Dir 'CMakeCache.txt') -Value @(
        '# This is the CMakeCache file.', "CMAKE_HOME_DIRECTORY:INTERNAL=$($Source.Replace('\', '/'))")
}

function Invoke-Guard([string]$BuildDir) {
    $output = & $pwsh -NoProfile -File (Join-Path $repo 'scripts\verify.ps1') -Clean -ValidateOnly `
        -BuildDir $BuildDir -SdkDir $sdk -QtDir $qt 2>&1 | Out-String
    return [pscustomobject]@{ Code = $LASTEXITCODE; Output = $output }
}

function Test-Refused([string]$Name, [string]$BuildDir) {
    $result = Invoke-Guard $BuildDir
    $missing = @(Get-ChildItem -LiteralPath $root -Recurse -Filter 'sentinel.txt' -File).Count -ne $script:sentinelCount
    if ($result.Code -eq 0 -or $result.Output -notmatch 'Refusing to clean' -or $missing) {
        $failures.Add("$Name was not refused cleanly (exit $($result.Code), sentinels lost: $missing): $($result.Output.Trim())")
    } else {
        Write-Host "ok   refused: $Name"
    }
}

function Test-Cleaned([string]$Name, [string]$BuildDir, [string]$ExpectedDir) {
    $result = Invoke-Guard $BuildDir
    if ($result.Code -ne 0 -or (Test-Path -LiteralPath $ExpectedDir)) {
        $failures.Add("$Name was not cleaned (exit $($result.Code)): $($result.Output.Trim())")
    } else {
        Write-Host "ok   cleaned: $Name"
    }
    $script:sentinelCount = @(Get-ChildItem -LiteralPath $root -Recurse -Filter 'sentinel.txt' -File).Count
}

try {
    New-Sentinel $repo
    New-Item -ItemType Directory -Force -Path (Join-Path $repo 'scripts') | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\scripts\verify.ps1') -Destination (Join-Path $repo 'scripts')
    git -C $repo init -q
    git -C $repo -c user.name=guard -c user.email=guard@example.invalid commit -q --allow-empty -m fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the fixture repository.' }

    New-Sentinel (Join-Path $repo 'build')
    New-Sentinel (Join-Path $repo 'src')
    New-Sentinel (Join-Path $sdk 'lib')
    New-Sentinel (Join-Path $qt 'lib')
    New-Sentinel (Join-Path $root 'plain')
    New-CMakeCache (Join-Path $root 'foreign-build') (Join-Path $root 'other-project')
    New-CMakeCache (Join-Path $root 'owned-build') $repo
    New-Sentinel (Join-Path $repo 'build\verify-release')
    New-Sentinel (Join-Path $repo 'build\relative')
    $script:sentinelCount = @(Get-ChildItem -LiteralPath $root -Recurse -Filter 'sentinel.txt' -File).Count

    Test-Refused 'the repository' $repo
    Test-Refused 'an ancestor of the repository' $root
    Test-Refused 'the build root itself' (Join-Path $repo 'build')
    Test-Refused 'a source folder inside the repository' (Join-Path $repo 'src')
    Test-Refused 'the SDK folder' $sdk
    Test-Refused 'a folder inside Qt' (Join-Path $qt 'lib')
    Test-Refused 'a ".." path escaping build\ into the SDK' (Join-Path $repo 'build\..\..\sdk')
    Test-Refused 'an unrelated folder without CMakeCache.txt' (Join-Path $root 'plain')
    Test-Refused 'a build folder of another project' (Join-Path $root 'foreign-build')

    Test-Cleaned 'a folder under build\' (Join-Path $repo 'build\verify-release') (Join-Path $repo 'build\verify-release')
    Test-Cleaned 'a relative folder under build\' 'build\relative' (Join-Path $repo 'build\relative')
    Test-Cleaned 'an outside build folder configured from this repository' (Join-Path $root 'owned-build') (Join-Path $root 'owned-build')
} catch {
    $failures.Add("Test setup failed: $($_.Exception.Message)")
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures.Count) {
    $failures | ForEach-Object { Write-Host "FAIL $_" }
    exit 1
}
Write-Host 'All verify.ps1 guard tests passed.'
exit 0
