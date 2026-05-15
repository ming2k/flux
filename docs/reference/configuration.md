# Configuration Reference

Configuration is split between Meson build options and runtime log settings.

## Meson options

| Key | Type | Default | Description |
|---|---|---|---|
| `examples` | boolean | `true` | Builds runnable examples when their dependencies are available. |
| `tests` | boolean | `true` | Builds the unit and integration test binaries. |
| `werror` | boolean | `true` | Treats compiler warnings as errors. |

## Runtime environment

| Key | Type | Default | Description |
|---|---|---|---|
| `VK_ICD_FILENAMES` | path list | loader-defined | Standard Vulkan loader variable for selecting an ICD, e.g. lavapipe for software rendering. |
| `VK_LAYER_PATH` | path list | loader-defined | Standard Vulkan loader variable for locating validation layers. |

## Context configuration

`flux_context_desc` fields set at context-creation time:

| Field | Type | Default | Description |
|---|---|---|---|
| `size` | `uint32_t` | required | Must be set to `sizeof(flux_context_desc)`. |
| `allocator` | `flux_allocator` | libc malloc | Optional custom allocator. Zero-initialize to use defaults. |
| `log` | `flux_log_fn` | `nullptr` (silent) | User-provided log callback. |
| `log_user` | `void*` | `nullptr` | Opaque pointer passed to `log` on every call. |
| `min_log_level` | `flux_log_level` | `FLUX_LOG_INFO` | Lowest severity emitted. `FLUX_LOG_DEBUG` and `FLUX_LOG_TRACE` only take effect in debug builds. |

## See also

- [How to link flux](../how-to/link-flux.md)
- [Developer setup](../dev/setup.md)
