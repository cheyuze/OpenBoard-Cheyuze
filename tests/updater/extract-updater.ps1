$ErrorActionPreference = 'Stop'
$sourceRoot = Join-Path $PSScriptRoot '../..'
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot '../src/core/UBApplicationController.cpp')) { $sourceRoot = Join-Path $PSScriptRoot '..' }
$source = Get-Content -LiteralPath (Join-Path $sourceRoot 'src/core/UBApplicationController.cpp') -Raw
$monitorStart = $source.IndexOf('    struct UBUpdateDownloadMonitor')
$monitorEnd = $source.IndexOf('    };', $monitorStart) + '    };'.Length
$methodStart = $source.IndexOf('void UBApplicationController::closeUpdateDownload(')
$methodEnd = $source.IndexOf('void UBApplicationController::checkAtLaunch()', $methodStart)
if ($monitorStart -lt 0 -or $monitorEnd -lt $monitorStart -or $methodStart -lt 0 -or $methodEnd -le $methodStart) { throw 'Cannot extract production updater methods' }
$methods = $source.Substring($methodStart, $methodEnd - $methodStart)
$locationCall = 'QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)'
if (($methods.Split(@($locationCall), [System.StringSplitOptions]::None)).Count -ne 2) { throw 'Unexpected download location expression' }
# Sole behavior substitution: keep all synthetic installers inside QTemporaryDir,
# never write to the user's Downloads. Production method bodies otherwise unchanged.
$methods = $methods.Replace($locationCall, 'updateTestDownloadDirectory')
$generated = '#include "updater_harness.h"' + "`n" + $source.Substring($monitorStart, $monitorEnd - $monitorStart) + "`n" + $methods
[System.IO.File]::WriteAllText((Join-Path $PSScriptRoot 'generated_updater.cpp'), $generated, [System.Text.UTF8Encoding]::new($false))
'Extracted production update methods; only the destination directory is redirected for isolation.'
