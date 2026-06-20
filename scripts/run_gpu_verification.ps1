# Real-GPU verification for Render2D (PowerShell; see run_gpu_verification.sh for
# the bash equivalent / CI path). Builds the full suite and runs it with the
# capability gates ARMED, so a green run is positive proof the GPU (and, with a
# display, the on-screen present) paths actually executed -- not just that they
# graceful-skipped (see the README "No GPU required").
#
#   -RequireGpu 0      do not arm render2d.gpu_presence_gate
#   -RequirePresent 0  do not arm render2d.present_capability_gate (set on a GPU
#                      server with no display)
#   -Presets a,b       which CMake presets to build/test (default: Debug + Perf)
[CmdletBinding()]
param(
    [string]$RequireGpu = '1',
    [string]$RequirePresent = '1',
    [string[]]$Presets = @('clang-ninja-debug', 'clang-ninja-perf')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir '..')
Set-Location $RepoRoot

function Invoke-Checked {
    param([Parameter(Mandatory)][string[]]$CommandLine)
    & $CommandLine[0] @($CommandLine[1..($CommandLine.Length - 1)])
    if ($LASTEXITCODE -ne 0) {
        throw "command failed ($LASTEXITCODE): $($CommandLine -join ' ')"
    }
}

Write-Host "Render2D GPU verification"
Write-Host "  RENDER2D_REQUIRE_GPU=$RequireGpu  RENDER2D_REQUIRE_PRESENT=$RequirePresent"
Write-Host "  presets: $($Presets -join ', ')"
Write-Host ""

foreach ($preset in $Presets) {
    Write-Host "=== configure + build: $preset ==="
    Invoke-Checked @('cmake', '--preset', $preset)
    Invoke-Checked @('cmake', '--build', '--preset', $preset)

    Write-Host "=== ctest (gates armed): $preset ==="
    $env:RENDER2D_REQUIRE_GPU = $RequireGpu
    $env:RENDER2D_REQUIRE_PRESENT = $RequirePresent
    Invoke-Checked @('ctest', '--preset', $preset)
    Write-Host ""
}

Write-Host "GPU verification complete ($($Presets -join ', '), gates armed)."
