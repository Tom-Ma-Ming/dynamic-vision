param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$Engine,
    [Parameter(Mandatory)][string]$Video,
    [Parameter(Mandatory)][string]$Tracker,
    [Parameter(Mandatory)][string]$Config,
    [Parameter(Mandatory)][string]$Output,
    [ValidateRange(1,86400)][int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$inputs = @($Engine,$Video,$Tracker,$Config) | ForEach-Object { (Resolve-Path -LiteralPath $_).Path }
$outputPath = [System.IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $outputPath) { throw 'Output directory already exists' }
New-Item -ItemType Directory -Path $outputPath | Out-Null
$arguments = @($inputs[0],$inputs[1],(Join-Path $outputPath 'replay'),$inputs[2],'--config',$inputs[3])
$argumentLine = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
$manifest = @{ executable=$exePath; arguments=$arguments; timeout_seconds=$TimeoutSeconds; inputs=@() }
foreach ($path in @($exePath)+$inputs) {
    $manifest.inputs += @{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $outputPath 'manifest.json') -Encoding utf8
$writer = [System.IO.StreamWriter]::new((Join-Path $outputPath 'resources.jsonl'))
$timer = [System.Diagnostics.Stopwatch]::StartNew()
$process = $null
try {
    $process = Start-Process -FilePath $exePath -ArgumentList $argumentLine -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $outputPath 'stdout.log') -RedirectStandardError (Join-Path $outputPath 'stderr.log')
    while (-not $process.HasExited) {
        if ($timer.Elapsed.TotalSeconds -gt $TimeoutSeconds) { throw 'Owned replay exceeded timeout' }
        $process.Refresh()
        if ($process.HasExited) { break }
        $sample = @{elapsed_s=$timer.Elapsed.TotalSeconds;pid=$process.Id;working_set_bytes=$process.WorkingSet64;private_bytes=$process.PrivateMemorySize64;cpu_seconds=$process.TotalProcessorTime.TotalSeconds;handles=$process.HandleCount}
        $writer.WriteLine(($sample | ConvertTo-Json -Compress)); $writer.Flush()
        Start-Sleep -Milliseconds 1000
    }
    $process.WaitForExit()
    @{exit_code=$process.ExitCode;elapsed_s=$timer.Elapsed.TotalSeconds} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath 'completion.json') -Encoding utf8
    if ($process.ExitCode -ne 0) { throw "Replay exited with $($process.ExitCode)" }
} finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    if ($process) { $process.Dispose() }
    $writer.Dispose()
}
