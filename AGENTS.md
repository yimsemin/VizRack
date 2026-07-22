# VizRack development guardrails

These rules apply to the whole repository. They document the intended native/web
boundary so a future developer or coding agent does not accidentally maintain two
implementations of a built-in visualizer.

## Source of truth for built-in visualizers

- `src/builtin/` is the only source of truth for built-in signal analysis, history,
  animation state, scene geometry, scene names, palettes, and option normalization.
- Do not copy a scene formula into `src/ui/` or a future web directory. A platform
  adapter supplies stereo float samples, calls an engine, and renders its `DrawList`.
- `src/builtin/` must remain free of Windows, GDI/GDI+, WASAPI, VST3, DOM, Canvas,
  filesystem, logging, and application-lifetime dependencies. Standard C++20 is the
  portability boundary.
- `src/ui/` owns Win32 input, menus, overlay text, timers, and GDI/GDI+ translation.
  A future web adapter may own Web Audio capture, Canvas translation, browser storage,
  and page controls, but it must consume the same built-in engines.
- The web product does not exist yet. Do not add a JavaScript/TypeScript copy of the
  engines as a shortcut; compile `vizrack_builtin_core` to WebAssembly when web work
  begins.

## Draw-list contract

- `builtin::DrawList` is intentionally a small retained buffer with an immediate-mode
  command vocabulary. Prefer composing existing primitives over adding abstractions.
- Adding or changing a `DrawPrimitive` is a cross-renderer contract change. Update the
  native renderer, future web renderer, core tests, and contract documentation together.
- Text and menus remain adapter-owned unless text placement becomes part of a scene's
  portable visual identity. Avoid adding font or localization dependencies to the core.
- Options exposed by settings use the public structs in the engine headers. Normalize
  invalid persisted values inside `setOptions` so every adapter gets identical behavior.

## Performance rules

- Keep audio capture non-blocking. Built-in engines receive bounded batches of at most
  4,096 stereo samples and never perform I/O.
- Do not introduce steady-state per-frame heap growth. Reuse `DrawList`, scratch vectors,
  circular histories, and the native back buffer. If a new scene exceeds current reserve
  sizes, update the reserve and the no-growth regression tests deliberately.
- Bound geometry by display resolution caps rather than generating unbounded point sets.
- Prefer measured frame time for animation/decay so 15, 30, and 60 FPS have comparable
  behavior.
- Keep the portable EXE small: avoid adding a UI framework, DSP framework, or serialization
  library for functionality already covered by the standard library and Win32.

## Required checks for built-in changes

1. Build `VizRack` and `VizRackTests` in Release.
2. Run `ctest --preset release` (or the equivalent configured build directory).
3. Run `VizRack.exe --smoke-test`.
4. For visual changes, manually inspect both built-in views with live audio at common and
   large window sizes. Tests validate geometry safety, not artistic equivalence.
5. When a web target exists, its build and renderer-contract tests become required in the
   same change; a built-in feature is not complete if only one adapter can render it.

See `docs/BUILTIN_VISUALIZER_CORE.md` for ownership, data flow, and the future web path.
