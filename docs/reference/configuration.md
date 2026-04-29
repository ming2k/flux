# Configuration Reference

Configuration is split between Meson build options and runtime environment variables.

## Meson options

| Key | Type | Default | Description |
|---|---|---|---|
| `wayland` | feature | `auto` | Enables Wayland integration and example support when dependencies are available. Values: `auto`, `enabled`, `disabled`. |
| `examples` | boolean | `true` | Builds runnable examples when their dependencies are available. |
| `tests` | boolean | `true` | Builds the unit and integration test binaries. |
| `validation` | feature | `auto` | Compiles Vulkan validation-layer support when available. Values: `auto`, `enabled`, `disabled`. |

## Runtime environment

| Key | Type | Default | Description |
|---|---|---|---|
| `FX_ENABLE_VALIDATION` | boolean-like | unset | Enables `VK_LAYER_KHRONOS_validation` at runtime when validation support was compiled in and the layer is installed. |
| `WAYLAND_DISPLAY` | string | compositor-defined | Selects the Wayland display used by Wayland examples and host applications. |
| `VK_INSTANCE_LAYERS` | string | unset | Standard Vulkan loader variable for enabling explicit layers such as `VK_LAYER_LUNARG_api_dump`. |
| `VK_ICD_FILENAMES` | path list | loader-defined | Standard Vulkan loader variable for selecting an ICD, useful for CI with lavapipe. |

## See also

- [How to link flux](../how-to/link-flux.md)
- [How to troubleshoot common issues](../how-to/troubleshooting.md)
- [Developer setup](../dev/setup.md)
