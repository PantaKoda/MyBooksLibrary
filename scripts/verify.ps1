<#
.SYNOPSIS
    The repository's single verification entry point (see docs/BUILDING.md):
    text checks, configure and build, tests, and application smoke checks.
    Hosted CI (.github/workflows/ci.yml, job build-and-test) runs this same script.

.DESCRIPTION
    Exits 0 only if every step passed; any failure exits non-zero. Finds MSVC
    (through vswhere/vcvars64 when cl.exe is not already on PATH), CMake and
    Ninja on its own. Prints the verified commit so results can be tied to a
    PR head.

.EXAMPLE
    pwsh scripts/verify.ps1 -SdkDir C:\Dev\pdfbookmark-sdk\0.3.0
.EXAMPLE
    pwsh scripts/verify.ps1 -Configuration Debug -Clean

.NOTES
    -Clean deletes only a build folder this script can identify as this
    project's build output: a folder under <repo>\build\, or one whose
    CMakeCache.txt names this repository as its source. The repository, its
    ancestors, and the SDK and Qt folders are always refused.
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    # Installed pdfbookmark SDK folder; defaults to the PDFBOOKMARK_SDK environment variable.
    [string]$SdkDir = $env:PDFBOOKMARK_SDK,
    # Qt kit folder (the one containing lib\cmake\Qt6); defaults to QT_ROOT_DIR, then the standard install.
    [string]$QtDir = $(if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { 'C:\Qt\6.11.2\msvc2022_64' }),
    # Build folder; defaults to build\verify-<configuration>.
    [string]$BuildDir = '',
    # Delete the build folder first (see .NOTES for which folders are allowed).
    [switch]$Clean,
    # Check the arguments and perform -Clean, then stop without building (used by
    # tools/test_verify_guards.ps1).
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo
if (-not $BuildDir) { $BuildDir = Join-Path $repo "build\verify-$($Configuration.ToLowerInvariant())" }
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $repo $BuildDir }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)  # Resolves "..", so checks below see the real target.
$steps = [System.Collections.Generic.List[object]]::new()

function Invoke-Step([string]$Name, [scriptblock]$Body) {
    Write-Host "==> $Name"
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & $Body
    $steps.Add([pscustomobject]@{ Step = $Name; Seconds = [math]::Round($timer.Elapsed.TotalSeconds, 1) })
}

function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe $($Arguments -join ' ') failed with exit code $LASTEXITCODE" }
}

function Add-ToolDir([string]$Tool, [string[]]$Candidates) {
    if (Get-Command $Tool -ErrorAction SilentlyContinue) { return }
    foreach ($dir in $Candidates) {
        if ($dir -and (Test-Path (Join-Path $dir "$Tool.exe"))) {
            $env:PATH = "$dir;$env:PATH"
            return
        }
    }
}

function Get-NormalizedDir([string]$Path) {
    if (-not $Path) { return $null }
    return [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}

# True if $Inner is $Outer itself or inside it (case-insensitive, Windows paths).
function Test-PathWithin([string]$Inner, [string]$Outer) {
    if (-not $Inner -or -not $Outer) { return $false }
    return $Inner.Equals($Outer, [StringComparison]::OrdinalIgnoreCase) -or
        $Inner.StartsWith($Outer + '\', [StringComparison]::OrdinalIgnoreCase)
}

# Refuses to delete anything this script cannot identify as this project's build output.
function Assert-SafeToClean([string]$Target) {
    $target = Get-NormalizedDir $Target
    $repoDir = Get-NormalizedDir $repo
    # The repository, the SDK, Qt and every folder containing one of them.
    foreach ($protected in @($repoDir, (Get-NormalizedDir $SdkDir), (Get-NormalizedDir $QtDir))) {
        if ($protected -and (Test-PathWithin $protected $target)) {
            throw "Refusing to clean '$target': it is or contains the protected folder '$protected'."
        }
    }
    # Anything inside the SDK or Qt.
    foreach ($protected in @((Get-NormalizedDir $SdkDir), (Get-NormalizedDir $QtDir))) {
        if ($protected -and (Test-PathWithin $target $protected)) {
            throw "Refusing to clean '$target': it is inside the protected folder '$protected'."
        }
    }
    $buildRoot = Join-Path $repoDir 'build'
    $underBuildRoot = (Test-PathWithin $target $buildRoot) -and
        -not $target.Equals($buildRoot, [StringComparison]::OrdinalIgnoreCase)
    if ((Test-PathWithin $target $repoDir) -and -not $underBuildRoot) {
        throw "Refusing to clean '$target': build folders inside the repository must be under '$buildRoot'."
    }
    if ($underBuildRoot) { return }
    # Outside the repository: only a CMake build folder configured from this repository.
    $cache = Join-Path $target 'CMakeCache.txt'
    $source = $null
    if (Test-Path -LiteralPath $cache -PathType Leaf) {
        $line = Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$' | Select-Object -First 1
        if ($line) { $source = Get-NormalizedDir $line.Matches[0].Groups[1].Value.Replace('/', '\') }
    }
    if (-not $source -or -not $source.Equals($repoDir, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean '$target': it is not under '$buildRoot' and has no CMakeCache.txt naming this repository as its source."
    }
}

function Get-PythonCommand {
    foreach ($candidate in @('python', 'py')) {
        if (Get-Command $candidate -ErrorAction SilentlyContinue) { return $candidate }
    }
    throw 'Python 3 is required for tools/check_text_files.py.'
}

try {
    $head = (git rev-parse HEAD).Trim()
    $dirty = [bool](git status --porcelain --untracked-files=no)
    Write-Host "Verifying $head ($Configuration)$(if ($dirty) { ' with uncommitted changes: the result does not describe that commit alone' })"

    # Check before any step so an unsafe -BuildDir is refused before anything is deleted.
    if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
        Assert-SafeToClean $BuildDir
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
        Write-Host "Cleaned $BuildDir"
    }
    if ($ValidateOnly) {
        Write-Host "VALIDATE-ONLY PASSED ($BuildDir)"
        exit 0
    }

    Invoke-Step 'Whitespace check (git diff --check, all tracked files)' {
        # Against Git's empty tree: every tracked file as in the working tree; binary files per .gitattributes are skipped.
        Invoke-Native git @('diff', '--check', '4b825dc642cb6eb9a060e54bf8d69288fbee4904')
    }

    Invoke-Step 'Text file check (UTF-8, no control characters)' {
        Invoke-Native (Get-PythonCommand) @('tools/check_text_files.py')
    }

    Invoke-Step 'Verification script guard tests' {
        Invoke-Native (Get-Process -Id $PID).Path @('-NoProfile', '-File', (Join-Path $repo 'tools\test_verify_guards.ps1'))
    }

    Invoke-Step 'Toolchain' {
        if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
            if (-not (Test-Path $vswhere)) { throw 'cl.exe is not on PATH and vswhere.exe was not found.' }
            $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if (-not $vs) { throw 'No Visual Studio installation with the x64 C++ tools was found.' }
            $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
            cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
                if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
            }
        }
        $qtTools = Join-Path $QtDir '..\..\Tools'
        $vsCMake = if ($env:VSINSTALLDIR) { Join-Path $env:VSINSTALLDIR 'Common7\IDE\CommonExtensions\Microsoft\CMake' } else { '' }
        Add-ToolDir 'cmake' @((Join-Path $qtTools 'CMake_64\bin'), $(if ($vsCMake) { Join-Path $vsCMake 'CMake\bin' }))
        Add-ToolDir 'ninja' @((Join-Path $qtTools 'Ninja'), $(if ($vsCMake) { Join-Path $vsCMake 'Ninja' }))
        foreach ($tool in 'cl', 'cmake', 'ninja') {
            if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool was not found." }
        }
        if (-not (Test-Path (Join-Path $QtDir 'lib\cmake\Qt6'))) { throw "Qt was not found at $QtDir (use -QtDir)." }
        if (-not $SdkDir -or -not (Test-Path (Join-Path $SdkDir 'lib\cmake\pdfbookmark'))) {
            throw "The pdfbookmark SDK was not found at '$SdkDir' (use -SdkDir or PDFBOOKMARK_SDK)."
        }
        $env:PATH = "$(Join-Path $QtDir 'bin');$env:PATH"  # Qt DLLs for the smoke checks.
        Write-Host "cmake: $((Get-Command cmake).Source)"
        Write-Host "ninja: $((Get-Command ninja).Source)"
        Write-Host "Qt:    $QtDir"
        Write-Host "SDK:   $SdkDir"
    }

    Invoke-Step "Configure ($Configuration)" {
        # MBL_BUILD_TESTS=ON overrides a reused cache that disabled the tests.
        Invoke-Native cmake @('-S', $repo, '-B', $BuildDir, '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$Configuration",
            "-DCMAKE_PREFIX_PATH=$QtDir", "-DPDFBOOKMARK_SDK=$SdkDir", '-DMBL_BUILD_TESTS=ON', '-Wno-dev')
    }

    Invoke-Step 'Build' {
        Invoke-Native cmake @('--build', $BuildDir)
    }

    Invoke-Step 'Tests (ctest)' {
        # --no-tests=error: an empty test suite is a failure, never a pass.
        Invoke-Native ctest @('--test-dir', $BuildDir, '--output-on-failure', '--timeout', '600', '--no-tests=error')
    }

    Invoke-Step 'Application smoke checks' {
        $app = Join-Path $BuildDir 'appMyBooksLibrary.exe'
        $fixtures = Join-Path $repo 'tests\fixtures'
        $previousPlatform = $env:QT_QPA_PLATFORM
        $env:QT_QPA_PLATFORM = 'offscreen'
        try {
            $checks = [ordered]@{
                'sdk-check'               = @('--sdk-check', (Join-Path $fixtures 'title-page.pdf'))
                'sqlite-check'            = @('--sqlite-check')
                'reader-check (text PDF)' = @('--reader-check', (Join-Path $fixtures 'contents-book.pdf'), '--rounds', '3')
            }
            foreach ($check in $checks.GetEnumerator()) {
                # Capturing the output also waits for this GUI-subsystem executable.
                $output = & $app @($check.Value) 2>&1 | Out-String
                $code = $LASTEXITCODE
                Write-Host "---- $($check.Key) (exit $code)"
                Write-Host $output
                if ($code -ne 0) { throw "$($check.Key) failed with exit code $code" }
            }
        } finally {
            $env:QT_QPA_PLATFORM = $previousPlatform
        }
    }

    $steps | Format-Table -AutoSize | Out-String | Write-Host
    Write-Host "VERIFY PASSED at $head ($Configuration)"
    exit 0
} catch {
    $steps | Format-Table -AutoSize | Out-String | Write-Host
    Write-Host "VERIFY FAILED: $($_.Exception.Message)"
    exit 1
}
