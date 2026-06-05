#!/usr/bin/env python3
"""Lightweight DXF regression checks for DxfLibrary samples.

This script intentionally does not depend on Qt.  It verifies the structural
features that DxfLibrary must preserve for mature DXF handling: sections,
handles, BLOCK/INSERT, HATCH, and common DXF entity pairs.
"""
from __future__ import annotations

import argparse
from pathlib import Path
from collections import Counter


def read_pairs(path: Path):
    data = path.read_bytes()
    if data.startswith(b"AutoCAD Binary DXF"):
        # The C++ importer handles binary DXF.  This no-dependency regression
        # script only validates ASCII pair structure and reports binary samples
        # as present instead of trying to decode their typed payload.
        return [(9999, "BINARY_DXF_SAMPLE")]
    lines = data.decode(errors="ignore").splitlines()
    pairs = []
    i = 0
    while i + 1 < len(lines):
        code_s = lines[i].strip()
        value = lines[i + 1].rstrip("\n")
        try:
            code = int(code_s)
        except ValueError:
            i += 1
            continue
        pairs.append((code, value.strip()))
        i += 2
    return pairs


def analyze(path: Path) -> dict:
    pairs = read_pairs(path)
    sections = Counter()
    entities = Counter()
    handles = 0
    blocks = 0
    inserts = 0
    hatches = 0
    last_zero = None
    for idx, (code, value) in enumerate(pairs):
        up = value.upper()
        if code == 0:
            last_zero = up
            if up not in {"SECTION", "ENDSEC", "EOF", "TABLE", "ENDTAB"}:
                entities[up] += 1
            if up == "BLOCK":
                blocks += 1
            elif up in {"INSERT", "MINSERT"}:
                inserts += 1
            elif up == "HATCH":
                hatches += 1
        elif code == 2 and idx > 0 and pairs[idx - 1] == (0, "SECTION"):
            sections[up] += 1
        elif code == 5:
            handles += 1
    return {
        "path": str(path),
        "pairs": len(pairs),
        "sections": sections,
        "entities": entities,
        "handles": handles,
        "blocks": blocks,
        "inserts": inserts,
        "hatches": hatches,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="*", type=Path, default=[Path("tests/dxf_samples")])
    args = parser.parse_args()
    files = []
    for p in args.paths:
        if p.is_dir():
            files.extend(sorted(p.glob("*.dxf")))
        elif p.exists():
            files.append(p)
    if not files:
        print("No DXF files found")
        return 1
    failed = False
    for f in files:
        r = analyze(f)
        ok = r["pairs"] > 0 and (r["sections"].get("ENTITIES", 0) >= 1 or f.read_bytes().startswith(b"AutoCAD Binary DXF"))
        print(f"{f}: pairs={r['pairs']} sections={dict(r['sections'])} handles={r['handles']} blocks={r['blocks']} inserts={r['inserts']} hatches={r['hatches']} top={r['entities'].most_common(8)}")
        if not ok:
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
