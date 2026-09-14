param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination
)
$ErrorActionPreference = 'Stop'
$sourceText = Get-Content -LiteralPath $Source -Raw -Encoding UTF8
$methodStart = $sourceText.IndexOf('void UBPodcastController::setSourceWidget(QWidget* pWidget)')
$methodEnd = $sourceText.IndexOf('UBPodcastController* UBPodcastController::instance()', $methodStart + 1)
if ($methodStart -lt 0 -or $methodEnd -le $methodStart) {
    throw 'Could not locate the production source-selector method; update the bounded extractor.'
}
$methodText = $sourceText.Substring($methodStart, $methodEnd - $methodStart).Trim()
[System.IO.File]::WriteAllText($Destination, $methodText, [System.Text.UTF8Encoding]::new($false))
