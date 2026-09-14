param(
    [Parameter(Mandatory=$true)][string]$SourceRoot,
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$QMake,
    [Parameter(Mandatory=$true)][string]$MakeTool,
    [string]$BaselineRef = ''
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
function Get-Method([string]$Text, [string]$Signature) {
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
    return $Text.Substring($start, $end - $start)
}
function Get-Source([string]$RelativePath) {
    if ($BaselineRef) {
        $lines = & git -C $SourceRoot show "${BaselineRef}:$RelativePath"
        if ($LASTEXITCODE -ne 0) { throw "Cannot read baseline $RelativePath" }
        return $lines -join "`n"
    }
    return Get-Content -LiteralPath (Join-Path $SourceRoot $RelativePath) -Raw -Encoding UTF8
}
$sidebar = Get-Source 'src/gui/UBBoardThumbnailsView.cpp'
$handlers = @(
    'setDocument', 'adjustThumbnail', 'centerOnThumbnail', 'ensureVisibleThumbnail',
    'updateActiveThumbnail', 'resizeEvent', 'updateThumbnailPixmap', 'mousePressEvent',
    'dragEnterEvent', 'dragMoveEvent', 'dropEvent'
) | ForEach-Object { Get-Method $sidebar ('void UBBoardThumbnailsView::' + $_ + '(') }
$board = Get-Source 'src/board/UBBoardView.cpp'
$panMethod = Get-Method $board 'void UBBoardView::mouseMoveEvent ('
# Compile the production middle-button branch unchanged; later tool branches
# belong to the full application's integration tests.
$panEnd = $panMethod.IndexOf('    if(!mIsDragInProgress', [StringComparison]::Ordinal)
if ($panEnd -lt 0) { throw 'Missing pan branch boundary' }
$handlers += $panMethod.Substring(0, $panEnd) + "`n}"
[IO.File]::WriteAllText((Join-Path $buildDir 'production_handlers.inc'), ($handlers -join "`n`n"))
Push-Location $buildDir
try {
    & $QMake (Join-Path $PSScriptRoot 'input-state.pro')
    if ($LASTEXITCODE -ne 0) { throw 'qmake failed' }
    & $MakeTool /NOLOGO
    if ($LASTEXITCODE -ne 0) { throw 'compile failed' }
} finally { Pop-Location }
