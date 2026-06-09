# ADR: ThreadCenter Runtime Infrastructure Boundary

Status: Accepted

Date: 2026-06-09

## Context

Stage 10 needs a path toward multi-threaded CPU pipeline work, and the host project already provides ThreadCenter at:

```text
E:/Project/MelosyneTest/ThreadCenter
```

Render2D must integrate this dependency without breaking its current architecture constraints:

- ECS-visible components remain Strict POD.
- Host ECS still owns component streams.
- Single-thread systems remain the correctness reference.
- ThreadCenter types must not appear in ECS component fields.
- Public Render2D component/system contracts should not broaden unless needed.

ThreadCenter currently exposes a header API target `Center.Thread.Headers` and requires CMake 3.28.

## Decision

Render2D embeds ThreadCenter with **header API only**:

- enable `Center.Thread.Headers`;
- disable ThreadCenter modules, examples, tests, benchmarks, and install rules;
- raise Render2D's CMake minimum to 3.28 to match the embedded dependency.

Render2D does **not** attach ThreadCenter directly to the public `Render2D::Render2D` interface target at this stage.

Instead, Render2D provides an internal build integration target:

```text
render2d_thread_runtime_support -> Render2D::Render2D + Center.Thread.Headers
```

This keeps ThreadCenter available for runtime/system infrastructure and tests, while public ECS/component headers remain ThreadCenter-free.

## Alternatives Considered

### Link ThreadCenter directly to `Render2D::Render2D`

Rejected for Stage 10G. That would broaden the public dependency surface before any public runtime header actually needs ThreadCenter.

### Delay ThreadCenter integration until 10H

Rejected. Stage 10G exists specifically to lock dependency shape, build constraints, and smoke coverage before multi-thread execution work begins.

### Put ThreadCenter types into ECS-visible components

Rejected. This would violate the Strict POD ECS boundary and couple host ECS data layout to runtime scheduler implementation details.

## Consequences

- Render2D can start 10H multi-thread runtime/system work on top of an already-embedded scheduler dependency.
- Public ECS/component contracts remain unchanged.
- The build now requires CMake 3.28 or newer.
- A dedicated smoke test proves ThreadCenter can execute a minimal ordered task plan inside the Render2D build.
- Future ThreadCenter-backed systems must still preserve deterministic output and keep the single-thread path as the correctness baseline.
