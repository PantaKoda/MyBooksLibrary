# Shared by scripts/verify.ps1 and scripts/package.ps1 (dot-sourced): runs
# native tools with checked exit codes, and puts MSVC, CMake, Ninja and Qt's
# bin folder on PATH, checking that Qt and the pdfbookmark SDK are present.

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

# MSVC (through vswhere/vcvars64 when cl.exe is not already on PATH), CMake and
# Ninja (Qt's Tools, else Visual Studio's), and Qt's bin folder.
function Initialize-Toolchain([string]$QtDir, [string]$SdkDir) {
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
