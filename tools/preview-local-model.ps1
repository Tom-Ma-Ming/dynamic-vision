param([string]$Source = '', [string]$Model = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $repo
if (-not $Source) { $Source = Join-Path $repo 'artifacts/datasets/yolo_v002/images/val' }
if (-not $Model) { $Model = Join-Path $repo 'artifacts/runs/yolo11n-v002-320/weights/best.onnx' }
if (!(Test-Path -LiteralPath $Source)) { throw "Missing source: $Source" }
if (!(Test-Path -LiteralPath $Model)) { throw "Missing model: $Model" }
$runName = 'preview-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,6)
$env:PYTHONUTF8 = '1'
& "$repo/.venv/Scripts/yolo.exe" predict "model=$Model" "source=$Source" imgsz=320 device=cpu save=True "project=$repo/artifacts/local-preview" "name=$runName"
if ($LASTEXITCODE -ne 0) { throw "Preview failed: $LASTEXITCODE" }
Write-Host "Results: $repo/artifacts/local-preview/$runName"
