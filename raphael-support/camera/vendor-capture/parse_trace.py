#!/usr/bin/env python3
"""Turn a kprobe trace of the vendor camera MMIO helpers into an ordered
(address, value) list and a per-region summary.

Usage: parse_trace.py cam_trace.txt [--region csiphy|csid|vfe|all]
"""
import re
import sys
from collections import OrderedDict

CAM_LO, CAM_HI = 0x0AC00000, 0x0AE00000

REGIONS = [
    ("csiphy0_lane", 0x0AC65000, 0x1000), ("csiphy0_glb", 0x0AC66000, 0x1000),
    ("csiphy1_lane", 0x0AC67000, 0x1000), ("csiphy1_glb", 0x0AC68000, 0x1000),
    ("csiphy2_lane", 0x0AC69000, 0x1000), ("csiphy2_glb", 0x0AC6A000, 0x1000),
    ("csiphy3_lane", 0x0AC6B000, 0x1000), ("csiphy3_glb", 0x0AC6C000, 0x1000),
    ("csid0", 0x0ACB3000, 0x1000), ("csid1", 0x0ACBA000, 0x1000),
    ("csid2", 0x0ACC8000, 0x1000), ("csid3", 0x0ACCF000, 0x1000),
    ("vfe0", 0x0ACAF000, 0x4000), ("vfe1", 0x0ACB6000, 0x4000),
    ("vfe_lite0", 0x0ACC4000, 0x4000), ("vfe_lite1", 0x0ACCB000, 0x4000),
    ("cpas", 0x0AC40000, 0x20000), ("camnoc", 0x0AC42000, 0x1000),
    ("camcc", 0x0AD00000, 0x10000),
]


def region_of(addr):
    for name, base, size in REGIONS:
        if base <= addr < base + size:
            return name, addr - base
    return None, None


def parse(path):
    writes = []
    line_re = re.compile(r"(camw|camwb|camr):\s*(.*)")
    for line in open(path, errors="replace"):
        m = line_re.search(line)
        if not m:
            continue
        kind = m.group(1)
        kv = dict(re.findall(r"(a\d)=([0-9a-fA-Fx]+)", m.group(2)))
        try:
            vals = [int(kv[k], 16) for k in sorted(kv)]
        except (KeyError, ValueError):
            continue
        # the helper takes a base pointer and an offset, or an absolute address;
        # keep both interpretations and resolve later
        writes.append((kind, vals, line.strip()))
    return writes


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    writes = parse(sys.argv[1])
    print(f"{len(writes)} helper calls parsed")

    hits = OrderedDict()
    for kind, vals, raw in writes:
        found = False
        for v in vals:
            name, off = region_of(v)
            if name:
                hits.setdefault(name, []).append((kind, vals, off, raw))
                found = True
                break
        if not found:
            hits.setdefault("outside-camera", []).append((kind, vals, None, raw))

    for name, items in hits.items():
        print(f"\n=== {name}: {len(items)} accesses")
        seen = OrderedDict()
        for kind, vals, off, _ in items:
            if off is None:
                continue
            seen[off] = (kind, vals)
        for off, (kind, vals) in list(seen.items())[:80]:
            print(f"  +0x{off:04x}  {' '.join('0x%x' % v for v in vals)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
