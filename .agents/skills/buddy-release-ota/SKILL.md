---
name: buddy-release-ota
description: Ship a buddy-kit firmware release to the fleet over OTA — preflight checks, version tag, build, upload to the hub, broadcast, verify slot flip, rollback. Use when releasing firmware, pushing an update to devices, broadcasting OTA, or rolling back a bad image.
---

# buddy: release over fleet OTA

Full OTA mechanics: `docs/ota.md`. Fleet flow: `docs/cloud.md` (paths
are from the repo root).

## Preflight

1. Clean tree: `git status` — no uncommitted changes ride into a release.
2. Docs contract honored (AGENTS.md table) — including an ADR if the
   release contains a material decision.
3. Version bumped: `BUDDY_FW_VERSION` in `apps/buddy/platformio.ini` —
   it's set per env, so bump all four. The device reports it via the
   `X-Fw` poll header; old-vs-new shows in the dashboard's fw column
   (NOT in the status ack), or Verify below can't tell them apart.
4. Device(s) on Wi-Fi with a hub URL set (`{"cmd":"status"}` shows both).

## Ship

```bash
# 1. tag
git tag v<X.Y.Z>

# 2. build
cd apps/buddy && pio run -e waveshare-esp32c6-touch-amoled-2-16

# 3. upload the image to the hub (R2 + index)
curl -X POST "<HUB>/v1/firmware?version=<X.Y.Z>" \
  -H "Authorization: Bearer $ADMIN_KEY" \
  --data-binary @.pio/build/waveshare-esp32c6-touch-amoled-2-16/firmware.bin

# 4. broadcast to every device
curl -X POST "<HUB>/v1/ota/broadcast" \
  -H "Authorization: Bearer $ADMIN_KEY" \
  -H "Content-Type: application/json" -d '{"version":"<X.Y.Z>"}'
```

## Verify

Each device picks up `{"cmd":"ota","url":…}` on its next poll, pulls,
flashes the inactive slot, reboots. Confirm: the dashboard row flips to
the new fw version, and the status ack's `ota.slot` flipped
(`ota_0` ↔ `ota_1`). Single device instead of broadcast:
`POST /v1/devices/:id/cmd`.

## Rollback

- The bootloader auto-rolls-back on repeated boot failure (dual-slot
  mechanism — `docs/decisions/006-dual-ota-partition-table.md`).
- A *running-but-wrong* image: broadcast the previous version — OTA
  doesn't care about version direction.
- A device that won't come back: USB reflash, worst case
  `../esp/boards/wsc6-amoled-2.16/board.py restore` from the buddy-kit
  repo root (esp is a sibling workspace). NVS/spiffs offsets are fixed,
  so reflashes preserve creds + characters.
