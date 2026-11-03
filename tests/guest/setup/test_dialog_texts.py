#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The setup dialog automation recognises every text the installer shows for a known outcome.

DGSETUI (the sysui route) and the other UI helpers read the installer's own
message box and accept only the messages listed in tools/setup/ui-verify.h,
matched byte for byte. The installer's texts live in tools/setup/main.cpp
(show_result). Editing one copy without the other passes every host test but
makes the guest gate reject a correct installation as an unknown dialog. This
checks that each text show_result displays for a recognised outcome (installed,
restart, restored, removed, busy) is in the table with its result code. The
table may also keep older installers' texts; those are not required here.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
main = (ROOT / 'tools/setup/main.cpp').read_text()
verify = (ROOT / 'tools/setup/ui-verify.h').read_text()

LITERAL = r'"(?:[^"\\]|\\.)*"'
SEQUENCE = re.compile(rf'{LITERAL}(?:\s*{LITERAL})*')


def text(sequence):
    """One C string from a run of adjacent literals."""
    raw = ''.join(part[1:-1] for part in re.findall(LITERAL, sequence))
    return raw.encode('latin-1').decode('unicode_escape')


result = main[main.index('void show_result(unsigned code, bool startup) {'):]
result = result[:result.index('\n}\n')]
# The restart prompts (results 11 and 16) come first, as the two arms of a conditional.
prompt = result[:result.index('MB_YESNO')]
restart = [text(m.group(0)) for m in SEQUENCE.finditer(prompt)]
assert len(restart) == 2 and all(t.endswith('?') for t in restart), restart
# Then the completion switch: case N: message = "...";
coded = {}
body = result[result.index('    switch (code) {'):]
for m in re.finditer(rf'case (\d+):\s*message = ({LITERAL}(?:\s*{LITERAL})*);', body):
    coded[int(m.group(1))] = text(m.group(2))
assert coded[0].startswith('DreamGPU is installed.'), coded

table = verify[verify.index('constexpr Message messages[] = {'):]
table = table[:table.index('\n    };\n')]
entries = {(text(m.group(1)), int(m.group(2)))
           for m in re.finditer(rf'\{{\s*({LITERAL}(?:\s*{LITERAL})*),\s*(\d+),\s*ID\w+\}}', table)}
assert len(entries) >= 7, entries

required = [(coded[c], c) for c in (0, 12, 13, 28)] + [(t, 11) for t in restart]
for message, code in required:
    assert (message, code) in entries, \
        f'installer shows {message!r} (result {code}) but ui-verify.h does not recognise it'
print(f'PASS setup dialog texts: all {len(required)} installer texts for recognised outcomes are in the UI table')
