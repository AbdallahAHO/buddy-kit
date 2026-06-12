# Architecture Decision Records

One record per material decision: a new dependency, a layer contract, a
memory/partition layout, a protocol shape, an infra choice. The ADR holds
the *why*; the matching `docs/*.md` holds the *how*. Convention adapted from
lokalise/lupo-ui's `docs/decisions/`, trimmed for a solo repo.

## Authoring checklist

- One decision per ADR. Title is the decision in active voice
  (`# NNN: <decision>`).
- One-line `**Decision:**` summary under the title — readable on its own.
- Spine: `## Context` → `## Decision` → `## Consequences`. Add
  `## Tradeoffs`, `## When to revisit`, `## Related` only when material.
- ≤40 lines target, 120 hard cap. Evidence and mechanics go in the matching
  `docs/*.md`; the ADR links to it.
- Status: `approved` at commit time (no review loop here). Never edit an
  approved ADR — write a successor and flip the old one's status to
  `superseded by NNN`.
- Update the index below in the same commit.

The `adr` skill (`.agents/skills/adr/`) walks through writing one.

## Index

- [001](001-injected-link-time-contracts.md) — Libraries receive app policy through injected link-time contracts
- [002](002-c6-heap-is-the-floor.md) — The C6's 512 KB heap is the floor: heapless contracts, no std::function
- [003](003-one-way-flow-and-the-ghost-rule.md) — State flows one way; out-of-territory painters full-clear on close
- [004](004-ldf-stays-on-chain-mode.md) — LDF stays on default chain mode
- [005](005-nvs-ends-at-0xe000.md) — NVS ends at 0xe000 so PlatformIO's boot_app0 write lands in otadata
- [006](006-dual-ota-partition-table.md) — Dual-OTA partition table keeps nvs/spiffs offsets fixed
- [007](007-devices-poll-the-hub.md) — Devices poll the hub instead of holding Durable Object connections
- [008](008-nimble-on-c6-bluedroid-on-s3.md) — NimBLE on the C6, Bluedroid on the S3 boards
- [009](009-agents-md-adrs-skills.md) — Agent rules live in vendor-neutral AGENTS.md; decisions live in ADRs; skills under .agents/skills/
