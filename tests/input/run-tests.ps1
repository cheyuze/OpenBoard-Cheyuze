param(
    [string]$SourceRoot = (Join-Path $PSScriptRoot '../..'),
    [string]$QMake = 'qmake.exe',
    [string]$MakeTool = 'nmake.exe',
    [string]$BaselineRef = '',
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$BuildTag = 'current'
)

$ErrorActionPreference = 'Stop'

# Use an x64 MSVC Developer PowerShell/command prompt. QMake can be either a
# command on PATH or the absolute path of the matching Qt MSVC installation.
$sourcePath = (Resolve-Path -LiteralPath $SourceRoot).Path
if (!(Test-Path -LiteralPath (Join-Path $sourcePath 'src/domain/UBGraphicsScene.cpp') -PathType Leaf)) {
    throw "SourceRoot must point to the OpenBoard repository: $sourcePath"
}
$qmakePath = (Get-Command $QMake -CommandType Application -ErrorAction Stop).Source
$makePath = (Get-Command $MakeTool -CommandType Application -ErrorAction Stop).Source
$qtBins = (& $qmakePath -query QT_INSTALL_BINS).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot query Qt binary directory' }
$qtPlugins = (& $qmakePath -query QT_INSTALL_PLUGINS).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot query Qt plugin directory' }
$buildRoot = Join-Path (Join-Path $PSScriptRoot 'build') $BuildTag
$inputBuild = Join-Path $buildRoot 'input-state'
$polygonBuild = Join-Path $buildRoot 'polygon-zoom'
$parameters = @{
    SourceRoot = $sourcePath
    QMake = $qmakePath
    MakeTool = $makePath
    BaselineRef = $BaselineRef
}

Write-Host "Production source: $sourcePath"
if ($BaselineRef) { Write-Host "Git baseline: $BaselineRef" }
Write-Host "Generated output: $buildRoot"
& (Join-Path $PSScriptRoot 'build-input-state.ps1') @parameters -BuildDir $inputBuild
& (Join-Path $PSScriptRoot 'build-polygon-zoom.ps1') @parameters -BuildDir $polygonBuild

$oldPath = $env:PATH
$oldPlatform = $env:QT_QPA_PLATFORM
$oldPlatformPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
$failures = @()
try {
    $env:PATH = $qtBins + [IO.Path]::PathSeparator + $env:PATH
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $qtPlugins 'platforms'
    $inputExecutable = Join-Path $inputBuild 'input_state_regression.exe'
    $cases = @(
        'null_resize', 'null_callbacks', 'document_reset', 'external_drop',
        'external_enter', 'valid_resize', 'lost_middle_release', 'held_middle'
    )
    # Run each input case in its own process: a regressed null dereference must
    # not prevent the remaining cases from being exercised.
    foreach ($case in $cases) {
        & $inputExecutable $case
        if ($LASTEXITCODE -ne 0) {
            $failures += "input-state/$case (exit $LASTEXITCODE)"
        }
    }
    & (Join-Path $polygonBuild 'polygon_zoom_regression.exe')
    if ($LASTEXITCODE -ne 0) {
        $failures += "polygon-zoom suite (exit $LASTEXITCODE)"
    }
} finally {
    $env:PATH = $oldPath
    $env:QT_QPA_PLATFORM = $oldPlatform
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $oldPlatformPath
}

if ($failures.Count) {
    throw ('Input regression failure(s): ' + ($failures -join ', '))
}
Write-Host 'PASS: input-state 8/8 and polygon-zoom 13/13 (21 cases).'
