#!/usr/bin/env python3
"""Print the 32x16 CoCo text screen from an XRoar snapshot.

usage: coco_screen.py SNAPSHOT MARKER_HEX MARKER_ADDR_HEX

The 64K RAM image is located by searching the snapshot for MARKER (bytes
known to be at MARKER_ADDR, e.g. the program's first instruction), so no
knowledge of the snapshot format is needed.  The last occurrence is used:
Disk BASIC's sector buffer, lower in RAM, may hold another copy.  Screen codes are decoded the
way Color BASIC's CHROUT stores them: $40-$5F letters, $60-$7F space,
digits and punctuation, $00-$3F inverse (lower case).  Trailing blanks
are trimmed; trailing empty lines are dropped.
"""
import sys

snap = open(sys.argv[1], "rb").read()
marker = bytes.fromhex(sys.argv[2])
addr = int(sys.argv[3], 16)
pos = snap.rfind(marker)
if pos < addr or pos - addr + 0x10000 > len(snap):
    sys.exit("marker not found in snapshot")
ram = snap[pos - addr:pos - addr + 0x10000]


def decode(v):
    if 0x40 <= v <= 0x5F:
        return chr(v)
    if 0x60 <= v <= 0x7F:
        return chr(v - 0x40)
    if 0x00 <= v <= 0x1F:
        return chr(v + 0x60)        # inverse video: lower case
    if 0x20 <= v <= 0x3F:
        return chr(v)
    return "#"                      # semigraphics


rows = ["".join(decode(ram[0x400 + 32 * r + c]) for c in range(32)).rstrip()
        for r in range(16)]
while rows and not rows[-1]:
    rows.pop()
print("\n".join(rows))
