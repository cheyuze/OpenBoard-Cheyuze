param(
    [Parameter(Mandatory=$true)][string]$SourceRoot,
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$QMake,
    [Parameter(Mandatory=$true)][string]$MakeTool,
    [string]$ControllerSource=''
)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
function Get-Method([string]$Text,[string]$Signature) {
    $start=$Text.IndexOf($Signature,[StringComparison]::Ordinal)
    if($start -lt 0){throw "Missing method: $Signature"}
    $open=$Text.IndexOf('{',$start)
    $depth=1;$end=$open+1
    while($depth -gt 0 -and $end -lt $Text.Length){
        if($Text[$end] -eq '{'){$depth++}
        if($Text[$end] -eq '}'){$depth--}
        $end++
    }
    if($depth -ne 0){throw "Unbalanced method: $Signature"}
    return $Text.Substring($start,$end-$start)
}
function Write-Generated([string]$Name,[string]$Body){
    [IO.File]::WriteAllText((Join-Path $BuildDir $Name),$Body,[Text.UTF8Encoding]::new($false))
}
$command=Get-Content -LiteralPath (Join-Path $SourceRoot 'src/domain/UBGraphicsItemUndoCommand.cpp') -Raw -Encoding UTF8
$scene=Get-Content -LiteralPath (Join-Path $SourceRoot 'src/domain/UBGraphicsScene.cpp') -Raw -Encoding UTF8
if(!$ControllerSource){$ControllerSource=Join-Path $SourceRoot 'src/board/UBBoardController.cpp'}
$board=Get-Content -LiteralPath $ControllerSource -Raw -Encoding UTF8
Write-Generated 'production_undo.inc' ((@('undo','redo') | ForEach-Object {Get-Method $command "void UBGraphicsItemUndoCommand::$($_)()"}) -join "`n`n")
Write-Generated 'production_stroke_repaint.inc' (Get-Method $scene 'void UBGraphicsScene::repaintStrokePreview(const QRectF& sceneRect)')
$handlers=@()
foreach($name in @('undo','redo','refreshUndoRedoViews')) {
    $signature="void UBBoardController::$($name)()"
    if($board.Contains($signature)){$handlers+=Get-Method $board $signature}
}
Write-Generated 'production_undo_controller.inc' ($handlers -join "`n`n")
$connections=[regex]::Matches($board,'(?m)^\s*connect\(mMainWindow->action(?:Undo|Redo),[^\r\n]*?(?:SLOT\((?:undo|redo)\(\)\)|&UBBoardController::(?:undo|redo))\);')
if($connections.Count -ne 2){throw 'Expected exactly two production undo/redo action connections'}
Write-Generated 'production_undo_connections.inc' (($connections | ForEach-Object {$_.Value.Trim()}) -join "`n")
Push-Location $BuildDir
try {
    & $QMake (Join-Path $PSScriptRoot 'undo-display.pro')
    if($LASTEXITCODE -ne 0){throw 'qmake failed'}
    & $MakeTool /nologo
    if($LASTEXITCODE -ne 0){throw 'compile failed'}
} finally {Pop-Location}
