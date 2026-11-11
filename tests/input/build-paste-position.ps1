param(
    [string]$SourceRoot = '',
    [Parameter(Mandatory=$true)][string]$ClientMakefile,
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
    [string]$BuildDir = '',
    [string]$TestSource = 'paste_position_native.cpp',
    [ValidatePattern('^[a-zA-Z0-9-]+$')][string]$TestName = 'paste-position'
)
$ErrorActionPreference = 'Stop'
if (!$SourceRoot) { $SourceRoot = Join-Path $PSScriptRoot '../..' }
if (!$BuildDir) { $BuildDir = Join-Path $PSScriptRoot 'build/paste-position' }
$sourcePath = (Resolve-Path -LiteralPath $SourceRoot).Path
$runtimePath = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$appPath = Join-Path $buildPath 'app'
New-Item -ItemType Directory -Force -Path (Join-Path $appPath 'etc') | Out-Null
$make = Get-Content -LiteralPath $ClientMakefile -Encoding UTF8
function Make-Value([string]$Name) {
    $line = @($make | Where-Object { $_ -match ('^' + $Name + '\s*=') })
    if ($line.Count -ne 1) { throw "Missing or ambiguous make variable $Name" }
    return $line[0] -replace ('^' + $Name + '\s*=\s*'), ''
}
$objectLines = @(); $collect = $false
foreach ($line in $make) {
    if ($line -match '^OBJECTS\s*=') { $collect = $true }
    if ($collect) {
        $objectLines += $line
        if (!$line.TrimEnd().EndsWith('\')) { break }
    }
}
$objects = @([regex]::Matches(($objectLines -join ' '), '[^\s=]+\.obj') | ForEach-Object { $_.Value })
if ($objects.Count -lt 100) { throw 'Incomplete client object list; build the full client first' }
$objects = @($objects | Where-Object { $_ -notmatch '(^|\\)main\.obj$' })
foreach ($object in $objects) {
    if (!(Test-Path -LiteralPath (Join-Path $sourcePath $object))) { throw "Missing client object $object" }
}
$testObject = Join-Path $buildPath ($TestName + '.obj')
$testExe = Join-Path $appPath ('OpenBoard-' + $TestName + '-tests.exe')
$compile = (Make-Value 'CXXFLAGS').Replace('$(DEFINES)', (Make-Value 'DEFINES')) + ' ' + (Make-Value 'INCPATH')
$compile += ' /c "' + (Join-Path $PSScriptRoot $TestSource) + '" /Fo"' + $testObject + '"'
$link = '/NOLOGO /DYNAMICBASE /NXCOMPAT /OPT:REF /OPT:ICF /INCREMENTAL:NO /SUBSYSTEM:CONSOLE /MANIFEST:embed /OUT:"' + $testExe + '"'
$link += "`r`n" + '"' + $testObject + '"' + "`r`n" + ($objects -join "`r`n") + "`r`n" + (Make-Value 'LIBS')
$compileRsp = Join-Path $buildPath 'compile.rsp'
$linkRsp = Join-Path $buildPath 'link.rsp'
# BOM-marked Unicode response files preserve non-ASCII workspace paths.
[IO.File]::WriteAllText($compileRsp, $compile, [Text.Encoding]::Unicode)
[IO.File]::WriteAllText($linkRsp, $link, [Text.Encoding]::Unicode)
Push-Location $sourcePath
try {
    & cl.exe "@$compileRsp"
    if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
    & link.exe "@$linkRsp"
    if ($LASTEXITCODE -ne 0) { throw 'Test linking failed' }
} finally { Pop-Location }

# Never read an installed app's settings or user data. The offscreen clipboard
# is process-local; this executable refuses a different platform/profile.
$profile = (Join-Path $buildPath ('paste-position-profile-' + [Guid]::NewGuid().ToString('N'))).Replace('\', '/')
$config = Get-Content -LiteralPath (Join-Path $sourcePath 'resources/etc/OpenBoard.config') -Raw -Encoding UTF8
$config = $config.Replace('[App]', "[App]`nDataDirectory=$profile`nUserImageDirectory=$profile/images`nUserVideoDirectory=$profile/videos`nUserAudioDirectory=$profile/audio")
$config = $config.Replace('EnableAutomaticSoftwareUpdates=true', 'EnableAutomaticSoftwareUpdates=false').Replace('EnableStartupHints=true', 'EnableStartupHints=false').Replace('UseMultiscreenMode=true', 'UseMultiscreenMode=false').Replace('PreferredLanguage=fr_CH', 'PreferredLanguage=zh_CN')
[IO.File]::WriteAllText((Join-Path $appPath 'etc/OpenBoard.config'), $config, [Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $sourcePath 'resources/etc/OpenBoard.css') -Destination (Join-Path $appPath 'etc/OpenBoard.css') -Force
foreach ($directory in @('library', 'i18n', 'startupHints')) {
    $from = Join-Path $runtimePath $directory
    if (Test-Path -LiteralPath $from) {
        $to = Join-Path $appPath $directory
        New-Item -ItemType Directory -Force -Path $to | Out-Null
        Get-ChildItem -LiteralPath $from | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $to -Recurse -Force }
    }
}
$qmake = (Make-Value 'QMAKE')
$qtBins = (& $qmake -query QT_INSTALL_BINS).Trim()
$qtPlugins = (& $qmake -query QT_INSTALL_PLUGINS).Trim()
$qtResources = (& $qmake -query QT_INSTALL_DATA).Trim() + '/resources'
$saved = @{}
foreach ($name in @('PATH', 'QT_QPA_PLATFORM', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QTWEBENGINE_RESOURCES_PATH', 'QTWEBENGINEPROCESS_PATH', 'QTWEBENGINE_CHROMIUM_FLAGS', 'QT_FORCE_STDERR_LOGGING', 'OPENBOARD_PASTE_TEST_PROFILE')) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:PATH = "$runtimePath;$qtBins;" + $env:PATH
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $qtPlugins 'platforms'
    $env:QTWEBENGINE_RESOURCES_PATH = $qtResources
    $env:QTWEBENGINEPROCESS_PATH = Join-Path $qtBins 'QtWebEngineProcess.exe'
    $env:QTWEBENGINE_CHROMIUM_FLAGS = '--disable-gpu --disable-background-networking'
    $env:QT_FORCE_STDERR_LOGGING = '1'
    $env:OPENBOARD_PASTE_TEST_PROFILE = $profile
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw "Paste-position test failed (exit $LASTEXITCODE)" }
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
