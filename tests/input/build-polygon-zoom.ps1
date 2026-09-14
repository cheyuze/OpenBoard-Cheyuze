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
function Get-Source([string]$RelativePath) {
    if ($BaselineRef) {
        $lines = & git -C $SourceRoot show "${BaselineRef}:$RelativePath"
        if ($LASTEXITCODE -ne 0) { throw "Cannot read baseline $RelativePath" }
        return $lines -join "`n"
    }
    return Get-Content -LiteralPath (Join-Path $SourceRoot $RelativePath) -Raw -Encoding UTF8
}
$scene = Get-Source 'src/domain/UBGraphicsScene.cpp'
$handlers = @(
    'void UBGraphicsScene::clearContent(clearCase pCase)',
    'bool UBGraphicsScene::polygonDrawingActive() const',
    'void UBGraphicsScene::cancelPolygonDrawing()',
    'void UBGraphicsScene::clearPolygonPreview()',
    'void UBGraphicsScene::clearShapeFillPreview()',
    'void UBGraphicsScene::updatePolygonPreview(const QPointF& scenePos)',
    'void UBGraphicsScene::commitPolygonDrawing()',
    'QGraphicsItem* UBGraphicsScene::rootItem(QGraphicsItem* item) const'
) | ForEach-Object { $_ + "`n" + (Get-Body $scene $_) }
$previewSignature = 'bool UBGraphicsScene::isPolygonPreviewItem(const QGraphicsItem* item) const'
if ($scene.Contains($previewSignature)) {
    $handlers += $previewSignature + "`n" + (Get-Body $scene $previewSignature)
} else {
    # The old version has no shared predicate. Its unfiltered behavior is kept.
    $handlers += $previewSignature + "`n{ Q_UNUSED(item); return false; }"
}
$handlers += 'void UBGraphicsScene::drawItems(QPainter* painter, int numItems, QGraphicsItem* items[], const QStyleOptionGraphicsItem options[], QWidget* widget)' + "`n" + (Get-Body $scene 'void UBGraphicsScene::drawItems (')
$svg = Get-Source 'src/adaptors/UBSvgSubsetAdaptor.cpp'
$writer = Get-Body $svg 'bool UBSvgSubsetAdaptor::UBSvgSubsetWriter::persistScene('
$filterStart = $writer.IndexOf('    while (!items.empty())', [StringComparison]::Ordinal)
$filterEnd = $writer.IndexOf('        // Is the item a polygon?', $filterStart, [StringComparison]::Ordinal)
if ($filterStart -lt 0 -or $filterEnd -lt 0) { throw 'Missing SVG item selection boundary' }
# Production item dequeue/preview filter unchanged. Later geometry serialization
# is outside this unit's scope; the retained visible polygon identities are checked.
$filter = $writer.Substring($filterStart, $filterEnd - $filterStart)
$handlers += 'QList<QGraphicsItem*> UBGraphicsScene::savedPolygonItems() const' + "`n{`n auto mScene = this;`n auto items = mScene->items();`n QList<QGraphicsItem*> selected;`n" + $filter + "`n if (dynamic_cast<UBGraphicsPolygonItem*>(item) && item->isVisible()) selected << item;`n }`n return selected;`n}"
$copyMethod = Get-Body $scene 'std::shared_ptr<UBGraphicsScene> UBGraphicsScene::sceneDeepCopy() const'
$copyStart = $copyMethod.IndexOf('if (ubItem &&', [StringComparison]::Ordinal)
$copyEnd = $copyMethod.IndexOf('{', $copyStart)
if ($copyStart -lt 0 -or $copyEnd -lt 0) { throw 'Missing copy item selection boundary' }
$copyCondition = $copyMethod.Substring($copyStart, $copyEnd - $copyStart)
$handlers += 'QList<QGraphicsItem*> UBGraphicsScene::copyEligiblePolygons() const' + "`n{`n QList<QGraphicsItem*> selected;`n for (auto item : items()) { auto ubItem = dynamic_cast<UBGraphicsPolygonItem*>(item);`n" + $copyCondition + " selected << item;`n }`n return selected;`n}"
$board = Get-Source 'src/board/UBBoardController.cpp'
$handlers += @(
    'void UBBoardController::updateZoomControl(qreal zoomFactor)',
    'void UBBoardController::setZoomPercentage(int percentage)'
) | ForEach-Object { $_ + "`n" + (Get-Body $board $_) }
$handlers += 'void UBBoardController::presetSelected(QAction* action)' + "`n" + (Get-Body $board 'connect(zoomPresetGroup, &QActionGroup::triggered, mZoomControl,')
$handlers += 'void UBBoardController::doubleClickReset()' + "`n" + (Get-Body $board 'zoomLabel->setDoubleClickHandler([this]()')
[IO.File]::WriteAllText((Join-Path $buildDir 'production_polygon_handlers.inc'), ($handlers -join "`n`n"))
Push-Location $buildDir
try {
    & $QMake (Join-Path $PSScriptRoot 'polygon-zoom.pro')
    if ($LASTEXITCODE -ne 0) { throw 'qmake failed' }
    & $MakeTool /NOLOGO
    if ($LASTEXITCODE -ne 0) { throw 'compile failed' }
} finally { Pop-Location }
