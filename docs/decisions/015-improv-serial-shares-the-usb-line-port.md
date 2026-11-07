# 015: Improv Serial shares the USB port with line-JSON, detected only at line start

**Decision:** glance answers Improv Serial v1 packets on its USB port alongside the JSON line protocol; a packet can only begin where a line would (the framer is empty and the byte is `I`), and Improv replies go to USB only, as binary packets.
**Status:** approved
**Date:** 2026-11-07

## Context

ESP Web Tools' "Configure Wi-Fi" button and the upcoming notify console
provision Wi-Fi over Web Serial with Improv Serial
(improv-wifi.com/serial), not our `{"cmd":"wifi",…}` line. The USB port
already carries newline-delimited JSON, log text and vdp stripes, and must
keep doing so. Improv packets are binary (`IMPROV` + version + type + len
+ data + checksum), so their bytes can include `\n`, `{` or anything else.

## Decision

The USB pump runs every byte through `lib/improv-serial` first. The parser
only claims a byte when the line framer is empty and the byte is `I`; it
then consumes bytes through the header, the declared length and the
checksum; every byte it doesn't claim goes to the framer. A line that
starts with `I` but isn't `IMPROV` is dropped, which loses nothing: valid
JSON lines start with `{`. A stalled packet resets after 200 ms.

Supported RPCs: Wi-Fi settings (joined through wifi-link, which persists
creds on success), get current state, get device info. Scan and the
hostname/name/network-state RPCs answer "unknown RPC". Improv has no
identify command over serial (0x02 is get-current-state). Replies are written
straight to USB rather than through `gLineOut` (rule 6): they are Improv
frames, not protocol lines, and mean nothing on the hub or socket. The
web flasher manifest sets `improv: true` for glance only.

## Consequences

The same port provisions Wi-Fi from ESP Web Tools, the notify console, or
our own `wifi` command. buddy is unchanged (no Improv). Mechanics and the
supported RPC table: `docs/connectivity.md`. A JSON line starting with `I`
is not a thing the protocol produces, so the only cost is the parser's
265-byte buffer.
