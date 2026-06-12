#pragma once

namespace Render2D {

template<class Provider, class Dim>
struct Transform;

template<class Provider, class Dim>
struct WorldTransform;

template<class Provider, class Dim>
struct TransformDirtyItem;

template<class Provider, class Dim>
struct Sprite;

template<class Provider, class Dim>
struct SpriteVertex;

template<class Provider, class Dim>
struct SpriteInstance;

template<class Provider, class Dim>
struct SpriteDrawPacket;

template<class Provider, class Dim>
struct SpriteMaterialBinding;

template<class Provider, class Dim>
struct SpriteTextureBinding;

struct TextureAtlasBuildConfig;

template<class Provider, class Dim>
struct TextureAtlasItem;

template<class Provider, class Dim>
struct TextureAtlasRegion;

template<class Provider, class Dim>
struct SpriteInstanceUploadCommand;

template<class Provider, class Dim>
struct Text;

template<class Provider, class Dim>
struct TextState;

template<class Provider, class Dim>
struct TextDirtyRange;

template<class Provider, class Dim>
struct Utf8Slice;

template<class Provider, class Dim>
struct GlyphRun;

template<class Provider, class Dim>
struct GlyphInstance;

template<class Provider, class Dim>
struct Codepoint;

template<class Provider, class Dim>
struct ShapingRun;

template<class Provider, class Dim>
struct ShapedGlyph;

template<class Provider, class Dim>
struct GlyphAtlasEntry;

template<class Provider, class Dim>
struct FontMetrics;

template<class Provider, class Dim>
struct Camera;

template<class Provider, class Dim>
struct LocalBounds;

template<class Provider, class Dim>
struct WorldBounds;

template<class Provider, class Dim>
struct VisibilityMask;

template<class Provider, class Dim>
struct RenderLayer;

template<class Provider, class Dim>
struct MaterialRef;

template<class Provider, class Dim>
struct TextureRef;

template<class Provider, class Dim>
struct FontRef;

template<class Provider, class Dim>
struct FontAtlasRef;

template<class Provider, class Dim>
struct VisibleItem;

template<class Provider, class Dim>
struct SortedItem;

template<class Provider, class Dim>
struct DrawCommand;

template<class Provider, class Dim>
struct BatchCommand;

template<class Provider, class Dim>
struct UploadCommand;

template<class Provider, class Dim>
struct NativeSubmitCommand;

template<class Provider, class Dim>
struct FrameIndex;

template<class Provider, class Dim>
struct FrameArenaState;

template<class Provider, class Dim>
struct CommandBuffer;

template<class Provider, class Dim>
struct UploadRingSlice;

template<class Provider, class Dim>
struct DescriptorSlice;

template<class Provider, class Dim>
struct FenceState;

template<class Provider, class Dim>
struct SwapchainImageRef;

template<class Provider, class Dim>
struct AcquiredImage;

template<class Provider, class Dim>
struct PresentCommand;

template<class Provider, class Dim>
struct DeferredDestroyCommand;

} // namespace Render2D
