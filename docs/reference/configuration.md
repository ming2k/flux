# Configuration Reference

Configuration is split between Meson build options and runtime environment variables.

## Meson options

| Key | Type | Default | Description |
|---|---|---|---|
| `examples` | boolean | `true` | Builds runnable examples when their dependencies are available. |
| `tests` | boolean | `true` | Builds the unit and integration test binaries. |
| `validation` | feature | `auto` | Compiles Vulkan validation-layer support when available. Values: `auto`, `enabled`, `disabled`. |

## Runtime environment

| Key | Type | Default | Description |
|---|---|---|---|
| `FX_ENABLE_VALIDATION` | boolean-like | unset | Enables `VK_LAYER_KHRONOS_validation` at runtime when validation support was compiled in and the layer is installed. |
| `VK_INSTANCE_LAYERS` | string | unset | Standard Vulkan loader variable for enabling explicit layers such as `VK_LAYER_LUNARG_api_dump`. |
| `VK_ICD_FILENAMES` | path list | loader-defined | Standard Vulkan loader variable for selecting an ICD, useful for CI with lavapipe. |

## Context configuration

`fx_context_desc` fields set at context-creation time:

| Field | Type | Default | Description |
|---|---|---|---|
| `log` | `fx_log_fn` | default `stderr` sink | User-provided log callback. Receives level, file, line, format string, formatted message, and user pointer. |
| `log_user` | `void*` | `nullptr` | Opaque pointer passed to `log` on every call. |
| `min_log_level` | `fx_log_level` | `FX_LOG_INFO` | Lowest severity that is formatted and emitted. `FX_LOG_DEBUG` and `FX_LOG_TRACE` only have effect in non-`NDEBUG` builds. |
| `enable_validation` | `bool` | `false` | Enables Vulkan validation layers at runtime (honored only if compiled with validation support). |
| `app_name` | `const char*` | `nullptr` | Application name passed to `vkCreateInstance`. |

## See also

- [How to link flux](../how-to/link-flux.md)
- [How to troubleshoot common issues](../how-to/troubleshooting.md)
- [Developer setup](../dev/setup.md)
