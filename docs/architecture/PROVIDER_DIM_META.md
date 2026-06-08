# Provider / Dim Meta Contract

???? Provider ? Dim ??????????????Render2D ??? runtime enum ????????

?????????

```cpp
Render2D::VulkanNativeProvider
Render2D::Dim2
```

?????

```cpp
Render2D::kSupportedProvider<Provider>
Render2D::kSupportedDim<Dim>
Render2D::SupportedRenderDomain<Provider, Dim>
Render2D::requireSupportedDomain<Provider, Dim>()
```

component ??????

```cpp
Render2D::ComponentTraits<Provider, Dim, Component>
Render2D::kSupportedComponent<Provider, Dim, Component>
Render2D::SupportedRenderComponent<Provider, Dim, Component>
```

## ????

???

```cpp
Transform<Provider, Dim>
Sprite<Provider, Dim>
CommandBuffer<Provider, Dim>
CommandBuildSystem<Provider, Dim>
EncodeSystem<Provider, Dim>
```

???

```cpp
Sprite2D
VulkanSprite
Vulkan2DSprite
OpenGLText
D3DCommand
```

?? alias ??????????????? public architecture?
