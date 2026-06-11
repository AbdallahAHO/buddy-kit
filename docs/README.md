# buddy-kit docs

Reading order if you're new:

| Doc | What it covers |
|---|---|
| [architecture.md](architecture.md) | The lego layers, dependency rules, repo layout, the React/atomic-design mapping |
| [rendering.md](rendering.md) | The frame loop, the three-layer render tree, pixel ownership (the ghost rule), display push |
| [data-flow.md](data-flow.md) | Transports → framer → dispatch → store; events; liveness; stats persistence |
| [input.md](input.md) | The focus ladder, semantic actions, overlays-as-data |
| [protocol.md](protocol.md) | The wire protocol: snapshots, commands/acks, file push, the hub HTTP contract |
| [connectivity.md](connectivity.md) | Wi-Fi pairing (QR + captive portal), wifi-link lifecycle, the HTTP hub transport |
| [hardware.md](hardware.md) | Boards, HAL pattern, partition table (and why it's shaped that way), USB quirks, flash/recovery |
| [extending.md](extending.md) | Recipes: add a screen, a menu row, a transport, a face, a board, an app |

Rules for keeping these honest are in the repo-root [CLAUDE.md](../CLAUDE.md):
every code change that touches a documented behavior updates its doc in the
same commit.
