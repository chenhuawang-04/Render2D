# Self-hosted GPU CI runner

The hosted `full-build` job (`.github/workflows/ci.yml`) builds the whole tree on a
free `ubuntu-latest` runner, but that runner has **no GPU** — so every
`render2d.vulkan_*` and `render2d.present_*` test graceful-skips there (green build,
GPU paths never executed; see the README *No GPU required*). To actually exercise
the GPU/present paths in CI you need a runner with a Vulkan-capable GPU. This is the
deferred *real-GPU CI* follow-up from the Stage 26 closeout
(`docs/adr/2026-06-20-stage26-reproducibility-ci-release-closeout.md`).

The workflow that uses such a runner is `.github/workflows/gpu.yml`. It is
**`workflow_dispatch`-only and inert** until a runner with the `gpu` label exists —
a push-triggered job targeting a self-hosted label would queue forever when no
runner is connected, so it deliberately never gates the hosted CI.

## What the GPU job proves

It runs the same Debug + Perf suite as `full-build`, but with the capability gates
**armed** via `scripts/run_gpu_verification.sh`:

- `RENDER2D_REQUIRE_GPU=1` arms `render2d.gpu_presence_gate` — a missing Vulkan
  instance/device becomes a hard failure instead of a skip.
- `RENDER2D_REQUIRE_PRESENT=1` arms `render2d.present_capability_gate` — a present
  path that cannot reach a TRANSFER-capable swapchain becomes a hard failure.

Because a present device makes every other GPU/present test run its real path, a
green run with the gates armed means the GPU paths genuinely executed this pass.

## Prerequisites on the runner machine

The job does **not** install a toolchain (unlike the hosted job); the runner must
already have:

- **Clang** (the presets use `clang++`) and **Ninja** and **CMake ≥ 3.28**.
- A **Vulkan SDK / loader + a GPU driver** (`find_package(Vulkan REQUIRE)` must
  succeed and a device must be present).
- **Git** with `bash` available (the workflow runs `bash scripts/...`; on Windows,
  Git for Windows provides this).
- The **engine dependencies** reachable — either a local checkout pointed at by
  `RENDER2D_ENGINE_DEPS_ROOT` (set it in the runner's environment) or network
  access so CMake can fetch the four public repos (pinned commits; no credentials).
- For `RENDER2D_REQUIRE_PRESENT=1`, an actual **display/session** (a desktop login).
  On a headless GPU server, trigger the workflow with `require_present = 0`.

A developer workstation that already builds Render2D (e.g. the maintainer's box)
satisfies all of this. You can also just run `scripts/run_gpu_verification.sh`
(or `.ps1`) on that box directly, without any runner — the runner only automates it.

## Registering the runner

1. In GitHub: **Settings → Actions → Runners → New self-hosted runner**, pick the OS,
   and follow the download/configure steps. When prompted for labels (or via
   `./config.cmd`/`./config.sh --labels gpu`), add the **`gpu`** label. The runner
   keeps the implicit `self-hosted` label, so it matches `runs-on: [self-hosted, gpu]`.
2. Start it (`./run.cmd` / `./run.sh`, or install it as a service).
3. Confirm it shows **Idle** under *Runners*. You can also list runners with:
   `gh api repos/chenhuawang-04/Render2D/actions/runners`.

## Running it

- GitHub UI: **Actions → "GPU verification (self-hosted)" → Run workflow** (set
  `require_present` to `0` for a headless GPU server).
- CLI: `gh workflow run "GPU verification (self-hosted)" -f require_present=1`.

## Security note

Render2D is a **private** repo, so only collaborators can push or open PRs and thus
only collaborators can trigger this workflow. The well-known risk of self-hosted
runners on **public** repos (a fork PR running arbitrary code on your machine) does
not apply here. If the repo is ever made public, switch the runner to ephemeral and
restrict who/what can trigger it before relying on it.
