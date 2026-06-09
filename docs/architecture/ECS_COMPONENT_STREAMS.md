# ECS Component Streams

Render2D treats render data as component streams plus stateless systems. An entity may be only an identity/index; component streams can also exist as derived frame data without one entity per record.

## Stream pipeline

```text
Sprite path:
Transform[] / Sprite[] / Camera[]
    -> WorldTransform[]
    -> WorldBounds[]
    -> VisibleItem[]
    -> DrawCommand[]

Text path:
Text[] + TextState[] + FontAtlasRef[]
    -> TextDirtyRange[]
    -> GlyphRun[]
    -> GlyphInstance[]
    -> DrawCommand[]

DrawCommand[]
    -> BatchCommand[]
    -> CommandBuffer[]
    -> NativeCommandBufferRef[]
    -> NativeSubmitCommand[]
```

Every stream above is an ECS component stream and every component remains Strict POD.

## Component / Storage / System boundary

- **Component**: Strict POD data record. It may store ids, handles, offsets, counts, flags, ranges, and scalar data only.
- **Storage**: owning memory or runtime table. Current `ComponentStreamStorage`, `FrameComponentStorage`, and `PersistentComponentStorage` are test-only ECS scaffolding under `tests/support/`.
- **System**: stateless component-stream transform. Systems use `std::span` as call boundaries only; `std::span` is not allowed as a component field.

`CommandBuffer<Provider, Dim>` is an ECS component, but only a POD descriptor containing draw/batch/upload/native-submit ranges. It does not own command arrays.

## Text / Glyph component streams

Stage 9 text data is also ECS-owned POD data:

```text
Text[]
TextState[]
TextDirtyRange[]
GlyphRun[]
GlyphInstance[]
```

`TextDirtySystem` compares `Text[]` against previous `TextState[]`, writes next `TextState[]`, and emits only changed `TextDirtyRange[]` entries. `GlyphRunBuildSystem::runDirty` and `GlyphInstanceBuildSystem::runDirty` update only those ranges, so static text does not need to rebuild glyph data every frame.

`GlyphBatchSystem` converts `GlyphRun[]` plus `FontAtlasRef[]` into regular `DrawCommand[]` entries whose `instance_first/instance_count` point into `GlyphInstance[]`. The existing `BatchSystem` then merges compatible glyph draws with the normal draw-command path.

## FreeType boundary

FreeType is vendored under `third_party/freetype/` but is not linked. Future font runtime work may use it for decoding/rasterization, but FreeType handles, font faces, glyph slots, atlas images, and caches must remain outside ECS components.
