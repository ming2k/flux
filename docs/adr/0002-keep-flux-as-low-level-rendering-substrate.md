# ADR-0002: Keep flux as a low-level rendering substrate

- **Status**: Accepted
- **Date**: 2026-04-29
- **Deciders**: project maintainers

## Context

flux sits between application/toolkit code and Vulkan. The project needs a clear boundary so API additions do not gradually turn it into a widget toolkit, layout engine, text stack, SVG parser, or general Vulkan framework.

## Decision

flux will stay a low-level rendering substrate. The core API owns surfaces, frame lifecycle, canvas recording, paths, paints, images, gradients, font render handles, positioned glyph runs, and backend execution. Layout, input, scene policy, text shaping/fallback, asset parsing, and application-owned Vulkan resources stay above flux.

## Alternatives considered

- **Grow into a retained UI toolkit**: Rejected because it would force widget, layout, styling, input, and accessibility policy into a library intended to be an explicit drawing layer.
- **Absorb document and asset parsing**: Rejected because SVG, CSS, image decoding, font discovery, and icon theme lookup have large dependency and policy surfaces better owned by applications or toolkits.
- **Expose all backend Vulkan objects through the core API**: Rejected because it would leak backend ownership into the portable drawing header and make future boundary changes harder.

## Consequences

- Positive: Public APIs remain explicit, small, and close to rendering work.
- Positive: Applications can pair flux with their own layout, shaping, and asset systems.
- Trade-off: Users must bring higher-level libraries for UI, text shaping, and asset ingestion.
- Negative (accepted): Simple applications need more setup code than they would with a retained toolkit.
