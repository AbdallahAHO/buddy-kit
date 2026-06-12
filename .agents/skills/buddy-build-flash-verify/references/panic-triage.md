# Panic and log triage

Adapted from `adamlipecz/esp32-firmware-engineer-skill` (panic-log-triage
reference), reshaped for buddy-kit's PlatformIO/Arduino toolchain — the
original assumes ESP-IDF (`idf.py`).

Use when diagnosing resets, Guru Meditation panics, boot loops, or unclear
runtime failures from serial logs.

## Collect the right data first

- Capture the full serial log from reset through failure, not only the
  panic tail: `pio device monitor --filter esp32_exception_decoder`
  (decodes backtraces against the just-built ELF automatically).
- Keep the ELF that matches the flashed binary:
  `.pio/build/<env>/firmware.elf`. A rebuilt ELF decodes to garbage.
- Note which transport/feature was active (BLE, portal, hub poll, file
  push) — the C6 is single-core; most failures are coexistence failures.

## Triage flow

1. Read the reset reason / panic headline (`Guru Meditation Error: …`).
2. Read the lines immediately *before* the panic for the triggering
   subsystem.
3. Decode the backtrace (monitor filter above, or
   `xtensa-esp32s3-elf-addr2line` / `riscv32-esp-elf-addr2line -pfiaC -e
   .pio/build/<env>/firmware.elf <addrs>` — the C6 is RISC-V).
4. Find the first app frame (not ESP-IDF internals) and check recent
   changes touching that task, buffer, or callback path.

## buddy-kit-specific suspects, in order

- **Heap exhaustion** on the C6 — ~109 KB free with Wi-Fi + BLE + portal
  up; anything new that allocates can starve the radio stacks. The status
  ack reports live heap; compare against `docs/hardware.md`'s budget.
- **Stack overflow in a FreeRTOS task** — transport-http's poll task and
  BLE callbacks are the usual offenders when logging or JSON parsing grows.
- **Non-ISR-safe calls from callbacks** — BLE/Wi-Fi event handlers run in
  stack tasks, not the loop.
- **Buffer lifetime crossing task boundaries** — the rings are SPSC by
  design; a second producer corrupts them.
- **Watchdog** — a blocking call in `loop()`; the loop thread must never
  block (that's why transport-http is a task).

## After the fix

A gotcha that cost an hour goes into the matching `docs/*.md` immediately
(see the docs contract in `AGENTS.md`).
