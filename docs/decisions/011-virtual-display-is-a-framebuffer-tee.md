# 011: The virtual display is a framebuffer tee, not a second renderer

**Decision:** Off-device visualization (browser preview, screenless boards, future composer) taps the one framebuffer every screen already draws into — `lib/virtual-display` ships changed canvas stripes as base64 JSON lines — instead of introducing a scene DSL with a device interpreter and a parallel web renderer.
**Status:** approved
**Date:** 2026-06-12

## Context

The long-run direction (web composer, screenless boards with a virtual
screen, agent-driven UI work) needs to *see* the display off-device. The
obvious designs both add an abstraction layer: a JSON scene format
interpreted on-device (a runtime the C6 doesn't have RAM to spare for,
and a DSL every agent would have to learn), or a TypeScript twin of the
renderer (two implementations of every pixel, drifting forever).

Reading the HAL dissolved the problem: every screen renders into one
software framebuffer (`hwCanvas()`), and `hwDisplayPush()` is the single
blit point. The pixels already exist in RAM, in one place.

## Decision

`lib/virtual-display` hashes the canvas in 2-row stripes (FNV-1a, fixed
table, heapless) and ships changed stripes as `{"vdp":"s",y,b}` JSON
lines over a `LineOut` the app chooses — USB only in the buddy app, since
stripe lines run ~1 KB. A 130-line static HTML viewer paints them over
WebSerial. UI code, screens, and the render loop are untouched; the tee
reads the buffer after the real push.

## Consequences

The browser shows the exact pixels the firmware rendered — nothing to
keep in sync, ever, and a screenless board gets a display by running the
same firmware with frames going only to the tee. Hardware-verified: full
184×224 keyframe in ~2 s over USB CDC, then dirty-stripe deltas. The
trade: previews can tear across stripes mid-animation, and bandwidth
bounds frame rate — acceptable for preview/capture, not video. Wire
shape in `docs/protocol.md`. The future upgrade path stays honest: compile
the same C++ to WASM for an instant simulator — a second compile target,
never a second implementation.
