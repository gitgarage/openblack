#!/usr/bin/env python3
"""Writes src/TextSamples.inc from the original runblack.exe: its text table (at 0x942b38, 6974 entries of
{text number, sound bank, sample}) says which voiced sample each RUN_TEXT number plays. Banks use
PLAY_SOUND_EFFECT's numbering (6 HelpSprites, 7 villagers, 10 Guidance); 0 is unvoiced.

    switch-port/extract-text-samples.py path/to/runblack.exe > src/TextSamples.inc
"""
import struct
import sys

data = open(sys.argv[1], 'rb').read()
base = 0x942b38 - 0x400000
rows = []
for number in range(0x1b3e):
    text, bank, sample = struct.unpack_from('<3I', data, base + number * 12)
    if text != number:
        sys.exit(f'entry {number} holds text {text}: not the text table this was written for')
    rows.append((bank, sample))
print("// Generated from runblack.exe's text table (0x942b38: 6974 entries of {text, sound bank, sample}) by")
print("// switch-port/extract-text-samples.py: RUN_TEXT's number -> the bank (PLAY_SOUND_EFFECT numbering) and sample")
print("// that voice it. Bank 0 is unvoiced.")
print("constexpr std::array<std::pair<uint8_t, uint16_t>, %d> k_TextSamples = {{" % len(rows))
for i in range(0, len(rows), 8):
    print("    " + ", ".join("{%d, %d}" % row for row in rows[i:i + 8]) + ",")
print("}};")
