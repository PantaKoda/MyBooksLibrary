<#
.SYNOPSIS
    Builds the Windows package of MyBooksLibrary: a folder (and a zip) that
    runs on its own, and checks it from a copy outside the repository.

.DESCRIPTION
    1. Toolchain (scripts/toolchain.ps1), then configure and build
       appMyBooksLibrary (Release, no tests) in build\package-release.
    2. Stage build\package\MyBooksLibrary:
       - the executable, with the pdfbookmark runtime DLLs and OCR models that
         pdfbookmark_deploy_runtime placed next to it;
       - Qt, through windeployqt: the QML imports found in qml\, only the
         SQLite SQL driver, no software OpenGL, D3D or DXC compiler, no QML
         debugging plugins, no translations;
       - the Visual C++ runtime DLLs, app-local (vcruntime, msvcp).
    3. licenses\: NOTICE.txt (what is shipped, under which licence), Qt's
       licence text and the SBOM of every Qt module whose files are shipped,
       the pdfbookmark SDK's third-party licences, and the OCR models' licence
       (from the SDK, else the pinned copy in third_party\licenses). A shipped
       Qt or SDK file whose licence is not known, or a missing licence, stops
       the script, so no notice is silently missing.
       Then every shipped binary's imports (dumpbin): anything neither shipped
       nor a known Windows component stops the script. Media Foundation, which
       Windows "N" editions lack without the Media Feature Pack, is reported.
    4. Smoke checks on a copy outside the repository, with PATH reduced to
       Windows' own folders, no Qt variables and the real platform, each with
       a time limit: --sdk-check, --sqlite-check,
       --reader-check on a text PDF and on a scanned one with the OCR models,
       and the window itself (real platform): import, read, save a copy with
       bookmarks through the Export dialog's session, close.
    5. build\package\MyBooksLibrary-<version>-win64.zip (unless -SkipZip).

.EXAMPLE
    pwsh scripts/package.ps1 -SdkDir C:\Dev\pdfbookmark-sdk\0.3.0
#>
[CmdletBinding()]
param(
    # Installed pdfbookmark SDK folder; defaults to the PDFBOOKMARK_SDK environment variable.
    [string]$SdkDir = $env:PDFBOOKMARK_SDK,
    # Qt kit folder (the one containing lib\cmake\Qt6); defaults to QT_ROOT_DIR, then the standard install.
    [string]$QtDir = $(if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { 'C:\Qt\6.11.2\msvc2022_64' }),
    # Qt's licence text; defaults to the pinned copy in third_party\licenses
    # (CI's Qt, from aqtinstall, has no Licenses folder).
    [string]$QtLicenseFile = '',
    # The PaddleOCR licence (Apache-2.0) for the OCR models in models\, used while
    # the SDK does not ship it itself (its licenses\PaddleOCR-PP-OCR-models.txt);
    # defaults to the pinned copy in third_party\licenses.
    [string]$ModelsLicenseFile = '',
    [switch]$SkipZip
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo
. (Join-Path $PSScriptRoot 'toolchain.ps1')

# Every output is a fixed folder under build\: nothing else is ever deleted.
$buildDir = Join-Path $repo 'build\package-release'
$packageRoot = Join-Path $repo 'build\package'
$stage = Join-Path $packageRoot 'MyBooksLibrary'
if (-not $QtLicenseFile) { $QtLicenseFile = Join-Path $repo 'third_party\licenses\Qt-LICENSE.txt' }
if (-not $ModelsLicenseFile) { $ModelsLicenseFile = Join-Path $repo 'third_party\licenses\PaddleOCR-LICENSE.txt' }

# Which Qt module each shipped Qt file comes from, by its path in the package
# (with forward slashes), for its SBOM and the notice. A file matching none of
# these stops the script.
$qtModules = [ordered]@{
    '^qml/QtQuick/Pdf/'                                         = 'qtpdf'
    '^qml/'                                                     = 'qtdeclarative'
    '^Qt6(Core|Gui|Network|Sql|OpenGL)\.dll$'                   = 'qtbase'
    '^Qt6(Qml|Quick|Labs)\w*\.dll$'                             = 'qtdeclarative'
    '^Qt6Pdf\w*\.dll$'                                          = 'qtpdf'
    '^Qt6Svg\.dll$'                                             = 'qtsvg'
    '^(platforms|generic|sqldrivers|tls|networkinformation)/'   = 'qtbase'
    '^imageformats/(qgif|qico|qjpeg)\.dll$'                     = 'qtbase'
    '^(imageformats/qsvg|iconengines/qsvgicon)\.dll$'           = 'qtsvg'
    '^imageformats/qpdf\.dll$'                                  = 'qtpdf'
}

# Runs the packaged executable with a time limit; a GUI-subsystem program that
# hangs (for example on an error dialog) fails the check instead of the run.
# Only the process started here is ever stopped.
function Invoke-App([string]$App, [string[]]$Arguments, [int]$TimeoutSeconds) {
    $info = [Diagnostics.ProcessStartInfo]::new($App)
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.UseShellExecute = $false
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        $process.WaitForExit()
        return [pscustomobject]@{ Code = 'timeout'; Output = "$($stdout.Result)$($stderr.Result)" }
    }
    $process.WaitForExit()
    return [pscustomobject]@{ Code = $process.ExitCode; Output = "$($stdout.Result)$($stderr.Result)" }
}

# The licence files (in the SDK's share\doc\pdfbookmark\licenses) that each
# shipped SDK component needs. A shipped SDK DLL not listed here, or a listed
# licence that is missing, stops the script: no SDK notice goes missing either.
$sdkNotices = [ordered]@{
    'pdfbookmark.dll'     = @()  # The SDK itself.
    'onnxruntime.dll'     = @('ONNX-Runtime.txt', 'ONNX-Runtime-third-party-notices.txt')
    'opencv_world500.dll' = @('OpenCV.txt')
    'pdfium.dll'          = @('PDFium.txt')
    'qpdf30.dll'          = @('qpdf.txt')
    'jpeg62.dll'          = @('libjpeg-turbo.txt')
    'z.dll'               = @('zlib.txt')
}
$modelsNotice = 'PaddleOCR-PP-OCR-models.txt'  # The OCR models in models\ (PaddleOCR PP-OCR, Apache-2.0).

# Imports the package may leave to Windows. Any other import of a shipped
# binary that the package does not ship itself stops the script, so "runs on
# a clean machine" is checked here, not only on one.
$windowsImports = '^(api-ms-win-[\w-]+|ext-ms-win-[\w-]+|advapi32|authz|bcrypt|comdlg32|crypt32|d3d9|d3d11|d3d12|dbghelp|dnsapi|dwmapi|dwrite|dxgi|gdi32|icuuc|imm32|iphlpapi|kernel32|mpr|ncrypt|netapi32|ntdll|ole32|oleaut32|secur32|setupapi|shell32|shlwapi|uiautomationcore|user32|userenv|uxtheme|version|winhttp|winmm|ws2_32|wtsapi32)\.dll$'
# Windows components missing from Windows "N" editions unless the Media
# Feature Pack is installed: allowed, and reported (docs/BUILDING.md).
$mediaFoundationImports = '^(mf|mfplat|mfreadwrite)\.dll$'

# The DLLs a binary imports: 'direct' ones are needed to start, 'delay' ones
# when first used (dumpbin /dependents, from the Visual C++ tools).
function Get-Imports([string]$Binary) {
    $imports = [System.Collections.Generic.List[object]]::new()
    $section = ''
    foreach ($line in (dumpbin /nologo /dependents $Binary)) {
        if ($line -match 'Image has the following dependencies') { $section = 'direct'; continue }
        if ($line -match 'Image has the following delay load dependencies') { $section = 'delay'; continue }
        if ($line -match '^\s*Summary') { $section = '' }
        if ($section -and $line -match '^\s+(\S+\.(dll|drv))\s*$') {
            $imports.Add([pscustomobject]@{ Kind = $section; Name = $Matches[1].ToLowerInvariant() })
        }
    }
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed on $Binary" }
    return $imports
}

function Get-QtModule([string]$RelativePath) {
    $path = $RelativePath.Replace([char]92, [char]47)  # Backslashes to slashes.
    foreach ($rule in $qtModules.GetEnumerator()) {
        if ($path -match $rule.Key) { return $rule.Value }
    }
    return $null
}

try {
    $version = ((Select-String -Path (Join-Path $repo 'CMakeLists.txt') -Pattern 'project\(MyBooksLibrary VERSION ([0-9.]+)').Matches[0].Groups[1].Value)
    $head = (git rev-parse --short HEAD).Trim()
    Write-Host "Packaging MyBooksLibrary $version ($head)"

    Write-Host '==> Toolchain'
    Initialize-Toolchain -QtDir $QtDir -SdkDir $SdkDir
    if (-not (Test-Path -LiteralPath $QtLicenseFile -PathType Leaf)) {
        throw "Qt's licence text was not found at '$QtLicenseFile' (use -QtLicenseFile)."
    }
    $windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'
    if (-not (Test-Path $windeployqt)) { throw "windeployqt was not found in $QtDir\bin." }
    $crtDir = Get-ChildItem -Directory -Path (Join-Path $env:VCToolsRedistDir 'x64') -Filter 'Microsoft.VC*.CRT' |
        Select-Object -First 1
    if (-not $crtDir) { throw "The Visual C++ runtime was not found under '$env:VCToolsRedistDir'." }

    Write-Host '==> Configure and build (Release)'
    Invoke-Native cmake @('-S', $repo, '-B', $buildDir, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_PREFIX_PATH=$QtDir", "-DPDFBOOKMARK_SDK=$SdkDir", '-DMBL_BUILD_TESTS=OFF', '-Wno-dev')
    Invoke-Native cmake @('--build', $buildDir, '--target', 'appMyBooksLibrary')

    Write-Host "==> Stage $stage"
    if (Test-Path -LiteralPath $packageRoot) { Remove-Item -LiteralPath $packageRoot -Recurse -Force }
    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item (Join-Path $buildDir 'appMyBooksLibrary.exe') $stage
    # What pdfbookmark_deploy_runtime placed next to the executable: the SDK's DLLs and models.
    Get-ChildItem -Path $buildDir -Filter '*.dll' -File | Copy-Item -Destination $stage
    Copy-Item -Recurse (Join-Path $buildDir 'models') (Join-Path $stage 'models')
    Invoke-Native $windeployqt @('--release', '--qmldir', (Join-Path $repo 'qml'), '--no-translations',
        '--no-opengl-sw', '--no-system-d3d-compiler', '--no-system-dxc-compiler', '--no-compiler-runtime',
        '--skip-plugin-types', 'qmltooling',
        '--exclude-plugins', 'qsqlodbc,qsqlpsql,qsqlmysql,qsqlibase,qsqloci,qsqlmimer',
        (Join-Path $stage 'appMyBooksLibrary.exe'))
    Get-ChildItem -Path $crtDir.FullName -Filter '*.dll' | Copy-Item -Destination $stage

    Write-Host '==> Notices'
    $licenses = Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Path (Join-Path $licenses 'qt') | Out-Null
    Copy-Item -LiteralPath $QtLicenseFile (Join-Path $licenses 'qt\LICENSE.txt')
    Copy-Item -Recurse (Join-Path $SdkDir 'share\doc\pdfbookmark\licenses') (Join-Path $licenses 'pdfbookmark')
    # The Qt module of every shipped Qt file (DLLs anywhere in the package).
    $sdkDlls = @(Get-ChildItem -Path (Join-Path $SdkDir 'bin') -Filter '*.dll' | ForEach-Object Name)
    $crtDlls = @(Get-ChildItem -Path $crtDir.FullName -Filter '*.dll' | ForEach-Object Name)
    $modules = [System.Collections.Generic.SortedSet[string]]::new()
    foreach ($dll in Get-ChildItem -Path $stage -Recurse -Filter '*.dll') {
        if ($dll.Directory.FullName -eq $stage -and ($sdkDlls -contains $dll.Name -or $crtDlls -contains $dll.Name)) { continue }
        $relative = $dll.FullName.Substring($stage.Length + 1)
        $module = Get-QtModule $relative
        if (-not $module) { throw "No Qt module is known for the shipped file $relative; add it to `$qtModules." }
        [void]$modules.Add($module)
    }
    foreach ($module in $modules) {
        $sbom = Join-Path $QtDir "sbom\$module-*.spdx.json"
        $found = @(Get-ChildItem -Path $sbom -ErrorAction SilentlyContinue)
        if (-not $found) { throw "The SBOM of Qt module $module was not found ($sbom)." }
        $found | Copy-Item -Destination (Join-Path $licenses 'qt')
    }
    # Every shipped SDK component with its licences, and the OCR models'.
    $sdkLicenses = Join-Path $licenses 'pdfbookmark'
    if (-not (Test-Path -LiteralPath (Join-Path $sdkLicenses $modelsNotice))) {
        if (-not $ModelsLicenseFile -or -not (Test-Path -LiteralPath $ModelsLicenseFile -PathType Leaf)) {
            throw "The SDK ships no licence for the OCR models in models\ ($modelsNotice). Pass the PaddleOCR licence " +
                  "(Apache-2.0, the LICENSE file of github.com/PaddlePaddle/PaddleOCR) with -ModelsLicenseFile."
        }
        Copy-Item -LiteralPath $ModelsLicenseFile (Join-Path $sdkLicenses $modelsNotice)
    }
    foreach ($dll in Get-ChildItem -Path $stage -Filter '*.dll' -File | Where-Object { $sdkDlls -contains $_.Name }) {
        if (-not $sdkNotices.Contains($dll.Name)) {
            throw "No licence is known for the shipped SDK file $($dll.Name); add it to `$sdkNotices."
        }
        foreach ($needed in $sdkNotices[$dll.Name]) {
            if (-not (Test-Path -LiteralPath (Join-Path $sdkLicenses $needed))) {
                throw "The licence $needed for $($dll.Name) is missing from the SDK's licences."
            }
        }
    }
    $sdkVersion = Split-Path -Leaf $SdkDir
    $qtVersion = (Split-Path -Leaf (Split-Path -Parent $QtDir))
    $notice = @"
MyBooksLibrary $version ($head), Windows x64

This package contains, besides MyBooksLibrary itself:

Qt $qtVersion (The Qt Company), modules: $($modules -join ', ')
  Used under the GNU Lesser General Public License v3 (licenses\qt\LICENSE.txt).
  The Qt libraries are separate DLLs next to the executable and may be
  replaced with compatible builds. Qt's source code is available at
  https://download.qt.io/official_releases/qt/. The third-party components
  inside each shipped Qt module (for example PDFium in Qt PDF) are listed,
  with their licences, in its SBOM: licenses\qt\<module>-$qtVersion.spdx.json.

pdfbookmark SDK $sdkVersion (PantaKoda/PDFMegine)
  Third-party licences: licenses\pdfbookmark\ (ONNX Runtime and its notices,
  OpenCV, PDFium, qpdf, libjpeg-turbo, zlib).

OCR models in models\: PaddleOCR PP-OCR models (PaddlePaddle Authors)
  Used under the Apache License 2.0 (licenses\pdfbookmark\$modelsNotice).

Microsoft Visual C++ runtime ($($crtDir.Name))
  Redistributed under the Microsoft Visual Studio license terms.

Windows "N" and "KN" editions need the Media Feature Pack: the OCR library
(OpenCV) uses Windows Media Foundation.
"@
    Set-Content -LiteralPath (Join-Path $licenses 'NOTICE.txt') -Value $notice -Encoding utf8
    Copy-Item -LiteralPath (Join-Path $licenses 'NOTICE.txt') (Join-Path $stage 'NOTICE.txt')

    Write-Host '==> Imports (what the package leaves to Windows)'
    $shipped = @{}
    Get-ChildItem -Path $stage -Recurse -Include '*.dll', '*.exe' | ForEach-Object { $shipped[$_.Name.ToLowerInvariant()] = $true }
    $mediaFoundation = [System.Collections.Generic.SortedSet[string]]::new()
    foreach ($binary in Get-ChildItem -Path $stage -Recurse -Include '*.dll', '*.exe') {
        foreach ($import in Get-Imports $binary.FullName) {
            if ($shipped.ContainsKey($import.Name) -or $import.Name -match $windowsImports) { continue }
            $relative = $binary.FullName.Substring($stage.Length + 1)
            if ($import.Name -match $mediaFoundationImports) {
                [void]$mediaFoundation.Add("$relative -> $($import.Name)")
            } elseif ($import.Kind -eq 'delay') {
                Write-Host "warning: $relative loads $($import.Name) when first used; it is neither shipped nor a known Windows component"
            } else {
                throw "$relative needs $($import.Name), which is neither shipped nor a known Windows component."
            }
        }
    }
    foreach ($entry in $mediaFoundation) {
        Write-Host "note: $entry (Media Foundation: Windows N editions need the Media Feature Pack)"
    }

    Write-Host '==> Smoke checks outside the repository'
    $trial = Join-Path ([IO.Path]::GetTempPath()) "mbl-package-$([guid]::NewGuid().ToString('N'))"
    $fixtures = Join-Path $repo 'tests\fixtures'
    $saved = @{ PATH = $env:PATH; QT_PLUGIN_PATH = $env:QT_PLUGIN_PATH; QML2_IMPORT_PATH = $env:QML2_IMPORT_PATH;
                QML_IMPORT_PATH = $env:QML_IMPORT_PATH; QT_QPA_PLATFORM = $env:QT_QPA_PLATFORM }
    try {
        Copy-Item -Recurse $stage (Join-Path $trial 'MyBooksLibrary')
        $app = Join-Path $trial 'MyBooksLibrary\appMyBooksLibrary.exe'
        # Only Windows' own folders: nothing from Qt, the SDK or the build.
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        foreach ($name in 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH') {
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }
        # The real platform, as users run it (the package ships no offscreen plugin).
        [Environment]::SetEnvironmentVariable('QT_QPA_PLATFORM', $null, 'Process')
        $checks = [ordered]@{
            'sdk-check'                 = @('--sdk-check', (Join-Path $fixtures 'title-page.pdf'))
            'sqlite-check (FTS5)'       = @('--sqlite-check')
            'reader-check (text PDF)'   = @('--reader-check', (Join-Path $fixtures 'contents-book.pdf'), '--rounds', '2')
            'reader-check (OCR models)' = @('--reader-check', (Join-Path $fixtures 'image-only.pdf'), '--rounds', '1', '--require-ocr')
        }
        foreach ($check in $checks.GetEnumerator()) {
            $run = Invoke-App $app $check.Value 300
            Write-Host "---- $($check.Key) (exit $($run.Code))"
            Write-Host $run.Output
            if ($run.Code -ne 0) { throw "$($check.Key) failed (exit $($run.Code))" }
        }
        # The window: import, read, export, close.
        $library = Join-Path $trial 'Library'
        $copy = Join-Path $trial 'Exports\Βιβλίο (bookmarked).pdf'
        New-Item -ItemType Directory -Path (Split-Path -Parent $copy) | Out-Null
        $run = Invoke-App $app @('--library', $library, '--import', (Join-Path $fixtures 'contents-book.pdf'),
            '--read-page', '15', '--export-first', $copy, '--close') 300
        Write-Host "---- window: import, read, export, close (exit $($run.Code))"
        Write-Host $run.Output
        if ($run.Code -ne 0) { throw "the window check failed (exit $($run.Code))" }
        if (-not (Test-Path -LiteralPath $copy -PathType Leaf)) { throw "the bookmarked copy was not written at $copy" }
        Write-Host "bookmarked copy: $((Get-Item -LiteralPath $copy).Length) bytes"
    } finally {
        foreach ($entry in $saved.GetEnumerator()) {
            [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
        }
        # Only the folder this run created in the temporary directory.
        if ($trial.StartsWith([IO.Path]::GetTempPath()) -and (Test-Path -LiteralPath $trial)) {
            Remove-Item -LiteralPath $trial -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    if (-not $SkipZip) {
        $zip = Join-Path $packageRoot "MyBooksLibrary-$version-win64.zip"
        Compress-Archive -Path $stage -DestinationPath $zip
        Write-Host "zip: $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
    }
    $size = (Get-ChildItem -Path $stage -Recurse -File | Measure-Object -Property Length -Sum).Sum
    Write-Host "PACKAGE PASSED: $stage ($([math]::Round($size / 1MB, 1)) MB, Qt modules: $($modules -join ', '))"
    exit 0
} catch {
    Write-Host "PACKAGE FAILED: $($_.Exception.Message)"
    exit 1
}
