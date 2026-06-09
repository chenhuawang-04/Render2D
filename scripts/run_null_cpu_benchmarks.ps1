[CmdletBinding()]
param(
    [string]$BenchmarkExe = "",
    [string]$OutputDir = "",
    [switch]$IncludeLarge,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir '..')

if ([string]::IsNullOrWhiteSpace($BenchmarkExe)) {
    $BenchmarkExe = Join-Path $RepoRoot 'build/bench/render2d_null_cpu_bench.exe'
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $RepoRoot 'build/bench_results'
}

$BenchmarkExe = [System.IO.Path]::GetFullPath($BenchmarkExe)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

if (-not (Test-Path -LiteralPath $BenchmarkExe -PathType Leaf)) {
    throw "Benchmark executable not found: $BenchmarkExe. Build with: cmake --build build --target render2d_null_cpu_bench"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$GeneratedAtUtc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
$Timestamp = (Get-Date).ToString('yyyyMMdd_HHmmss')
$CsvPath = Join-Path $OutputDir "null_cpu_baseline_$Timestamp.csv"
$MarkdownPath = Join-Path $OutputDir "null_cpu_baseline_$Timestamp.md"

$Scenarios = @(
    [pscustomobject]@{
        Id = 'sprite_high_10k'
        Purpose = 'Sprite pipeline, high visibility baseline.'
        Args = @('--scenario', 'sprite', '--sprites', '10000', '--frames', '8', '--warmup', '2', '--visibility', 'high', '--format', 'csv')
    },
    [pscustomobject]@{
        Id = 'sprite_low_10k'
        Purpose = 'Sprite pipeline, low visibility culling baseline.'
        Args = @('--scenario', 'sprite', '--sprites', '10000', '--frames', '8', '--warmup', '2', '--visibility', 'low', '--format', 'csv')
    },
    [pscustomobject]@{
        Id = 'text_static_2k'
        Purpose = 'Text pipeline with stable text after warmup.'
        Args = @('--scenario', 'text', '--texts', '2048', '--frames', '8', '--warmup', '2', '--glyphs-per-text', '8', '--dirty-text-stride', '0', '--format', 'csv')
    },
    [pscustomobject]@{
        Id = 'text_dirty_2k'
        Purpose = 'Text pipeline with deterministic partial dirty updates.'
        Args = @('--scenario', 'text', '--texts', '2048', '--frames', '8', '--warmup', '2', '--glyphs-per-text', '8', '--dirty-text-stride', '8', '--format', 'csv')
    },
    [pscustomobject]@{
        Id = 'mixed_10k_2k'
        Purpose = 'Combined sprite and text command-stream baseline.'
        Args = @('--scenario', 'mixed', '--sprites', '10000', '--texts', '2048', '--frames', '8', '--warmup', '2', '--visibility', 'high', '--glyphs-per-text', '8', '--dirty-text-stride', '8', '--format', 'csv')
    }
)

if ($IncludeLarge) {
    $Scenarios += @(
        [pscustomobject]@{
            Id = 'sprite_high_100k'
            Purpose = 'Large sprite pipeline baseline, high visibility.'
            Args = @('--scenario', 'sprite', '--sprites', '100000', '--frames', '8', '--warmup', '2', '--visibility', 'high', '--format', 'csv')
        },
        [pscustomobject]@{
            Id = 'sprite_low_100k'
            Purpose = 'Large sprite pipeline baseline, low visibility.'
            Args = @('--scenario', 'sprite', '--sprites', '100000', '--frames', '8', '--warmup', '2', '--visibility', 'low', '--format', 'csv')
        },
        [pscustomobject]@{
            Id = 'text_dirty_10k'
            Purpose = 'Large text dirty-update baseline.'
            Args = @('--scenario', 'text', '--texts', '10000', '--frames', '8', '--warmup', '2', '--glyphs-per-text', '8', '--dirty-text-stride', '8', '--format', 'csv')
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
        throw "Benchmark scenario '$($Scenario.Id)' failed with exit code $ExitCode. Output: $Message"
    }

    $ParsedRows = @($Output | ConvertFrom-Csv)
    if ($ParsedRows.Count -ne 1) {
        $Message = ($Output | Out-String).Trim()
        throw "Benchmark scenario '$($Scenario.Id)' did not produce one CSV row. Output: $Message"
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
$Markdown.Add('# Render2D Null CPU Benchmark Run') | Out-Null
$Markdown.Add('') | Out-Null
$Markdown.Add("- Generated UTC: $GeneratedAtUtc") | Out-Null
$Markdown.Add("- Benchmark executable: ``$BenchmarkExe``") | Out-Null
$Markdown.Add("- CSV: ``$CsvPath``") | Out-Null
$Markdown.Add('') | Out-Null
$Markdown.Add('| Scenario | Visible | Total Draws | Batches | Transform ms | Bounds ms | Culling ms | Sprite Cmd ms | Text Dirty ms | Glyph Instance ms | Batch ms |') | Out-Null
$Markdown.Add('|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|') | Out-Null
foreach ($Record in $Records) {
    $Markdown.Add("| $($Record.scenario_id) | $($Record.visible) | $($Record.total_draws) | $($Record.batches) | $($Record.avg_transform_ms) | $($Record.avg_bounds_ms) | $($Record.avg_culling_ms) | $($Record.avg_sprite_command_ms) | $($Record.avg_text_dirty_ms) | $($Record.avg_glyph_instance_ms) | $($Record.avg_batch_ms) |") | Out-Null
}
$Markdown | Set-Content -Path $MarkdownPath -Encoding utf8

if (-not $Quiet) {
    Write-Host "Wrote CSV: $CsvPath"
    Write-Host "Wrote Markdown: $MarkdownPath"
}

[pscustomobject]@{
    generated_at_utc = $GeneratedAtUtc
    csv_path = $CsvPath
    markdown_path = $MarkdownPath
    scenario_count = $Records.Count
}
