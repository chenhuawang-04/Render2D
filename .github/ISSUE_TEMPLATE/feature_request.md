---
name: Enhancement (within renderer scope)
about: Propose an improvement to Render2D's rendering backend
title: "[enhancement] "
labels: enhancement
---

<!-- Before opening: Render2D is a 2D *rendering module*, NOT a game engine.
     Windowing/app loop, input, audio, the production ECS/scene graph, the asset
     pipeline, animation, physics, gameplay/scripting, particles/tilemaps/UI,
     editor tooling, and networking are out of scope (the host engine's job at
     merge) — see docs/ARCHITECTURE.md → "Scope and non-goals". Proposals for those
     will be closed as out-of-scope. -->

## Problem / motivation

What renderer-side limitation are you hitting?

## Proposed change

What you'd like, and where it fits (component / system / native runtime / build /
docs). Note any impact on the non-negotiable invariants (Strict POD components,
`std::span` system boundaries, `McVector`/MemoryCenter memory, `id + generation`
refs, `VulkanNativeProvider + Dim2`).

## Alternatives considered

Other approaches, and why this one.
