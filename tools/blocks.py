#!/usr/bin/env python3
"""Copy-in source blocks (the shadcn move): a block is real app source plus
the whole-line rows it owns in the app's central tables. `add` copies the
files and inserts the rows; `rm` is the exact inverse; after iterating
in-app, `export` refreshes the block's file copies from the app.

Usage:
    python3 tools/blocks.py add    <block> [--app apps/buddy]
    python3 tools/blocks.py rm     <block> [--app apps/buddy]
    python3 tools/blocks.py verify <block> [--app apps/buddy]
    python3 tools/blocks.py export <block> [--app apps/buddy]

block.json shape (paths relative to the app dir / the block dir):
    {
      "name": "...", "description": "...", "legos": ["wifi-link"],
      "files": { "files/foo.cpp": "src/screens/foo.cpp" },
      "patches": [
        { "file": "src/main.cpp",
          "anchor": ["exact existing line", "...optional next line"],
          "insert": "after" | "before",
          "lines": ["the whole line(s) the block owns"] }
      ]
    }

Patches are whole lines only — anchors must match exactly once, so every
hook point in the app is a single removable line (see docs/extending.md,
"Blocks"). add/rm are idempotent: adding twice or removing twice is a no-op.
"""
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def fail(msg):
    sys.exit(f"blocks: {msg}")


def load(block):
    bdir = REPO / "blocks" / block
    spec_path = bdir / "block.json"
    if not spec_path.is_file():
        fail(f"no such block: blocks/{block}/block.json")
    return bdir, json.loads(spec_path.read_text())


def find_seq(lines, seq):
    """Indices where seq appears as consecutive lines."""
    hits = []
    for i in range(len(lines) - len(seq) + 1):
        if lines[i:i + len(seq)] == seq:
            hits.append(i)
    return hits


def locate_anchor(path, lines, anchor):
    hits = find_seq(lines, anchor)
    if len(hits) != 1:
        fail(f"{path}: anchor {anchor[0]!r} matches {len(hits)} times (need exactly 1)")
    return hits[0]


def patch_add(app, p):
    path = app / p["file"]
    lines = path.read_text().split("\n")
    if find_seq(lines, p["lines"]):
        return False                       # already applied
    i = locate_anchor(p["file"], lines, p["anchor"])
    at = i + len(p["anchor"]) if p["insert"] == "after" else i
    lines[at:at] = p["lines"]
    path.write_text("\n".join(lines))
    return True


def patch_rm(app, p):
    path = app / p["file"]
    lines = path.read_text().split("\n")
    hits = find_seq(lines, p["lines"])
    if not hits:
        return False                       # already removed
    if len(hits) > 1:
        fail(f"{p['file']}: block lines appear {len(hits)} times — refusing to guess")
    del lines[hits[0]:hits[0] + len(p["lines"])]
    path.write_text("\n".join(lines))
    return True


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = [a for a in sys.argv[1:] if a.startswith("--")]
    if len(args) != 2:
        fail(__doc__.strip().split("\n")[0] + " — see tools/blocks.py --help")
    cmd, block = args
    app = REPO / next((o.split("=", 1)[1] for o in opts if o.startswith("--app=")), "apps/buddy")
    bdir, spec = load(block)

    if cmd == "add":
        for src, dst in spec["files"].items():
            dest = app / dst
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text((bdir / src).read_text())
        n = sum(patch_add(app, p) for p in spec["patches"])
        print(f"add {block}: {len(spec['files'])} files, {n} patches applied")

    elif cmd == "rm":
        n = sum(patch_rm(app, p) for p in spec["patches"])
        for dst in spec["files"].values():
            (app / dst).unlink(missing_ok=True)
        print(f"rm {block}: {len(spec['files'])} files removed, {n} patches reverted")

    elif cmd == "verify":
        ok = True
        for src, dst in spec["files"].items():
            if not (app / dst).is_file():
                print(f"missing: {dst}"); ok = False
            elif (app / dst).read_text() != (bdir / src).read_text():
                print(f"drifted: {dst} (export to refresh the block)"); ok = False
        for p in spec["patches"]:
            if not find_seq((app / p["file"]).read_text().split("\n"), p["lines"]):
                print(f"unpatched: {p['file']} ({p['lines'][0][:50]!r})"); ok = False
        print(f"verify {block}: {'ok' if ok else 'FAILED'}")
        sys.exit(0 if ok else 1)

    elif cmd == "export":
        for src, dst in spec["files"].items():
            (bdir / src).parent.mkdir(parents=True, exist_ok=True)
            (bdir / src).write_text((app / dst).read_text())
        print(f"export {block}: {len(spec['files'])} files refreshed from {app.name}")

    else:
        fail(f"unknown command {cmd!r} (add | rm | verify | export)")


if __name__ == "__main__":
    main()
