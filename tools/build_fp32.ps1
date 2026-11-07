param(
    [Parameter(Mandatory)][string]$Onnx,
    [Parameter(Mandatory)][string]$Output,
    [Parameter(Mandatory)][string]$Sdk,
    [string]$CudnnBin
)
$ErrorActionPreference = "Stop"
$exe = (Resolve-Path "$Sdk/bin/trtexec.exe").Path
$model = (Resolve-Path $Onnx).Path
$dir = (New-Item -ItemType Directory -Path $Output).FullName
$env:Path = "$Sdk/lib;$CudnnBin;$env:Path"
$engine = Join-Path $dir "best-fp32.engine"
$options = @("--onnx=$model", "--saveEngine=$engine", "--noTF32", "--memPoolSize=workspace:512", "--skipInference")
& $exe @options *> "$dir/build.log"
if ($LASTEXITCODE -ne 0) { throw "Engine build failed: $dir/build.log" }
$gpu = & nvidia-smi --query-gpu=name,compute_cap,driver_version --format=csv,noheader
@{
    onnx_sha256 = (Get-FileHash $model).Hash
    engine_sha256 = (Get-FileHash $engine).Hash
    trtexec = $exe
    trtexec_sha256 = (Get-FileHash $exe).Hash
    sdk_runtime_sha256 = (Get-FileHash "$Sdk/lib/nvinfer.dll").Hash
    gpu = $gpu
    arguments = $options
    build_utc = [DateTime]::UtcNow.ToString("o")
} | ConvertTo-Json | Set-Content -Encoding utf8 "$dir/manifest.json"
