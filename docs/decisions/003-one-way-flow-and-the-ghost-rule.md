# 003: State flows one way; out-of-territory painters full-clear on close

**Decision:** Render functions never write shared state and input code never paints; anything that paints outside the buddy/HUD territories must full-clear (`applyDisplayMode()` + buddy invalidate) when it closes — the ghost rule.
**Status:** approved
**Date:** 2026-06-11

## Context

The pre-refactor app mixed paint calls into input handlers and state
writes into draw functions, which made "why did the screen change" a
whole-program question. Separately, the renderer only repaints regions it
knows are dirty — so when an overlay, QR screen, or any full-screen
surface closed without telling anyone, its pixels stayed behind as ghosts.
A real bug class: the fix landed as "full-canvas repaint when full-screen
surfaces close" after ghosting showed up on hardware.

## Decision

Adopt the React/atomic-design discipline described in
`docs/architecture.md`: `loop()` = pump → update store → route input →
render. Shared state lives in `app_state.h`; screen-local state stays
inside its `screens/*.cpp`. Draw functions may *read* the store directly
(the deliberate non-React compromise — prop structs would double the code)
but never mutate it. Every surface that paints outside the buddy/HUD
territories owns a close path that full-clears and invalidates the buddy.

## Consequences

Pixel ownership is auditable per surface (`docs/rendering.md` maps the
territories), and state changes are traceable to input or transport events
only. The cost is ceremony: every new overlay/screen must wire its close
path (the `docs/extending.md` recipes bake this in), and forgetting it
still compiles — ghosts are caught by the physical UI checklist, not the
compiler.

## Related

- `docs/rendering.md` — the render tree and territory map (the how).
- `docs/input.md` — the focus ladder that owns all input routing.
