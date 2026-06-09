[CmdletBinding()]
param(
    [string]$BuildDir = "",
    [string]$BenchmarkExe = "",
    [string]$OutputDir = "",
    [switch]$IncludeLarge,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir '..')

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot 'build_perf'
}
elseif (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot $BuildDir
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

if ([string]::IsNullOrWhiteSpace($BenchmarkExe)) {
    $BenchmarkExe = Join-Path $BuildDir 'bench/render2d_threaded_cpu_pipeline_bench.exe'
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $BuildDir 'bench_results'
}

$BenchmarkExe = [System.IO.Path]::GetFullPath($BenchmarkExe)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

if (-not (Test-Path -LiteralPath $BenchmarkExe -PathType Leaf)) {
    throw "Benchmark executable not found: $BenchmarkExe. Build with: cmake --build `"$BuildDir`" --target render2d_threaded_cpu_pipeline_bench"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$GeneratedAtUtc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
$Timestamp = (Get-Date).ToString('yyyyMMdd_HHmmss')
$CsvPath = Join-Path $OutputDir "threaded_cpu_pipeline_$Timestamp.csv"
$MarkdownPath = Join-Path $OutputDir "threaded_cpu_pipeline_$Timestamp.md"

$Scenarios = @(
    [pscustomobject]@{
        Id = 'threaded_sprite_high_10k_w1'
        Purpose = 'Threaded runtime one-worker overhead/reference check.'
        Args = @('--sprites', '10000', '--frames', '8', '--warmup', '2', '--visibility', 'high', '--workers', '1', '--min-items-per-task', '1024')
    },
    [pscustomobject]@{
        Id = 'threaded_sprite_high_10k_w4'
        Purpose = 'Threaded runtime four-worker high-visibility check.'
        Args = @('--sprites', '10000', '--frames', '8', '--warmup', '2', '--visibility', 'high', '--workers', '4', '--min-items-per-task', '1024')
    },
    [pscustomobject]@{
        Id = 'threaded_sprite_low_10k_w4'
        Purpose = 'Threaded runtime four-worker low-visibility check.'
        Args = @('--sprites', '10000', '--frames', '8', '--warmup', '2', '--visibility', 'low', '--workers', '4', '--min-items-per-task', '1024')
    }
)

if ($IncludeLarge) {
    $Scenarios += @(
        [pscustomobject]@{
            Id = 'threaded_sprite_high_100k_w4'
            Purpose = 'Large threaded runtime four-worker high-visibility check.'
            Args = @('--sprites', '100000', '--frames', '8', '--warmup', '2', '--visibility', 'high', '--workers', '4', '--min-items-per-task', '2048')
        },
        [pscustomobject]@{
            Id = 'threaded_sprite_low_100k_w4'
            Purpose = 'Large threaded runtime four-worker low-visibility check.'
            Args = @('--sprites', '100000', '--frames', '8', '--warmup', '2', '--visibility', 'low', '--workers', '4', '--min-items-per-task', '2048')
        }
    )
}

$Records = New-Object System.Collections.Generic.List[object]

foreach ($Scenario in $Scenarios) {
    if (-not $Quiet) {
        Write-Host "Running $($Scenario.Id)..."
    }

    $Output = & $BenchmarkExe @($Scenario.Args) 2>&1
    $ExitCode = $LASTEXITCODE
    if ($ExitCode -ne 0) {
        $Message = ($Output | Out-String).Trim()
        throw "Threaded benchmark scenario '$($Scenario.Id)' failed with exit code $ExitCode. Output: $Message"
    }

    $ParsedRows = @($Output | ConvertFrom-Csv)
    if ($ParsedRows.Count -ne 1) {
        $Message = ($Output | Out-String).Trim()
        throw "Threaded benchmark scenario '$($Scenario.Id)' did not produce one CSV row. Output: $Message"
    }

    $Row = $ParsedRows[0]
    $Record = [ordered]@{
        generated_at_utc = $GeneratedAtUtc
        scenario_id = $Scenario.Id
        purpose = $Scenario.Purpose
    }
    foreach ($Property in $Row.PSObject.Properties) {
        $Record[$Property.Name] = $Property.Value
    }
    $Records.Add([pscustomobject]$Record) | Out-Null
}

$Records | Export-Csv -Path $CsvPath -NoTypeInformation -Encoding utf8

$Markdown = New-Object System.Collections.Generic.List[string]
$Markdown.Add('# Render2D Threaded CPU Pipeline Benchmark Run') | Out-Null
$Markdown.Add('') | Out-Null
$Markdown.Add("- Generated UTC: $GeneratedAtUtc") | Out-Null
$Markdown.Add("- Build directory: ``$BuildDir``") | Out-Null
$Markdown.Add("- Benchmark executable: ``$BenchmarkExe``") | Out-Null
$Markdown.Add("- CSV: ``$CsvPath``") | Out-Null
$Markdown.Add('') | Out-Null
$Markdown.Add('| Scenario | Sprites | Visibility | Workers | Visible | Draws | Batches | Reference ms | Threaded ms | Speedup |') | Out-Null
$Markdown.Add('|---|---:|---|---:|---:|---:|---:|---:|---:|---:|') | Out-Null
foreach ($Record in $Records) {
    $Markdown.Add("| $($Record.scenario_id) | $($Record.sprites) | $($Record.visibility) | $($Record.actual_workers) | $($Record.visible) | $($Record.draws) | $($Record.batches) | $($Record.avg_reference_total_ms) | $($Record.avg_threaded_total_ms) | $($Record.threaded_speedup) |") | Out-Null
}
$Markdown | Set-Content -Path $MarkdownPath -Encoding utf8

if (-not $Quiet) {
    Write-Host "Wrote CSV: $CsvPath"
    Write-Host "Wrote Markdown: $MarkdownPath"
}

[pscustomobject]@{
    generated_at_utc = $GeneratedAtUtc
    build_dir = $BuildDir
    csv_path = $CsvPath
    markdown_path = $MarkdownPath
    scenario_count = $Records.Count
}
