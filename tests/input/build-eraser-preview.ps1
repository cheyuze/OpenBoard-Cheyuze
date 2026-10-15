param(
    [Parameter(Mandatory=$true)][string]$SourceRoot,
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$QMake,
    [Parameter(Mandatory=$true)][string]$MakeTool,
    [string]$BaselineRef = ''
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
function Get-Body([string]$Text, [string]$Signature) {
    $start = $Text.IndexOf($Signature, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing method: $Signature" }
    $open = $Text.IndexOf('{', $start)
    $depth = 1
    $end = $open + 1
    while ($depth -gt 0 -and $end -lt $Text.Length) {
        if ($Text[$end] -eq '{') { ++$depth }
        if ($Text[$end] -eq '}') { --$depth }
        ++$end
    }
    if ($depth -ne 0) { throw "Unbalanced method: $Signature" }
    return $Text.Substring($open, $end - $open)
}
if ($BaselineRef) {
    $lines = & git -c "safe.directory=$($SourceRoot.Replace('\','/'))" -C $SourceRoot show "${BaselineRef}:src/domain/UBGraphicsScene.cpp"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read baseline scene source' }
    $sceneSource = $lines -join "`n"
} else {
    $sceneSource = Get-Content -LiteralPath (Join-Path $SourceRoot 'src/domain/UBGraphicsScene.cpp') -Raw -Encoding UTF8
}
$handlers = @()
$helperSignature = 'void updateEraserPreview(UBBoardView* view, const QRectF& previousRect, const QRectF& currentRect = QRectF())'
# Historical code has no explicit invalidation helper. Do not replace its
# preview methods with fixed copies: they must fail the stale-region tests.
if ($sceneSource.Contains($helperSignature)) {
    $handlers += $helperSignature + "`n" + (Get-Body $sceneSource $helperSignature)
}
foreach ($signature in @(
    'void UBGraphicsScene::drawEraser(const QPointF &pPoint, bool pressed)',
    'void UBGraphicsScene::redrawEraser(bool pressed)',
    'void UBGraphicsScene::hideEraser()'
)) {
    $handlers += $signature + "`n" + (Get-Body $sceneSource $signature)
}
[IO.File]::WriteAllText((Join-Path $BuildDir 'production_eraser_handlers.inc'), ($handlers -join "`n`n"))
Push-Location $BuildDir
try {
    & $QMake (Join-Path $PSScriptRoot 'eraser-preview.pro')
    if ($LASTEXITCODE -ne 0) { throw 'qmake failed' }
    & $MakeTool /NOLOGO
    if ($LASTEXITCODE -ne 0) { throw 'compile failed' }
} finally { Pop-Location }
