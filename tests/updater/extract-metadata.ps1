$ErrorActionPreference = 'Stop'
$sourceRoot = Join-Path $PSScriptRoot '../..'
$source = Get-Content -LiteralPath (Join-Path $sourceRoot 'src/core/UBApplicationController.cpp') -Raw
$start = $source.IndexOf('void UBApplicationController::checkUpdate(')
$end = $source.IndexOf('void UBApplicationController::initPreviousViews()', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Cannot extract production metadata methods' }
$generated = '#include "metadata_harness.h"' + "`n" + $source.Substring($start, $end - $start)
[System.IO.File]::WriteAllText((Join-Path $PSScriptRoot 'generated_metadata.cpp'), $generated, [System.Text.UTF8Encoding]::new($false))
'Extracted unchanged production metadata request and response methods.'
