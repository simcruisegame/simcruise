"""Write verdicts into a review.md (scripts/ReviewLib.ps1 sheets).

Usage: python -I scripts/set-verdicts.py <review.md> <verdicts.json> <reviewer>

verdicts.json maps an item's name, or its first words, to [verdict, evidence]. The evidence is
added after the numbers already in the row. An item missing from the sheet (an older sheet) is
added. The verdict is PASS, FAIL or N/A.
"""
import json
import sys

sheet, verdicts_path, reviewer = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(sheet, encoding='utf-8-sig').read()
nl = '\r\n' if '\r\n' in text else '\n'
lines = text.split(nl)
verdicts = json.load(open(verdicts_path, encoding='utf-8-sig'))
used = set()
for i, line in enumerate(lines):
    if not line.startswith('| ') or line.startswith('| Item ') or line.startswith('|---'):
        continue
    cells = line.split(' | ')
    name = cells[0][2:]
    for key, (verdict, evidence) in verdicts.items():
        if name.startswith(key):
            old = cells[2].rstrip(' |') if len(cells) > 2 else ''
            ev = (old + '; ' if old else '') + evidence
            lines[i] = f'| {name} | {verdict} | {ev.replace("|", "/")} |'
            used.add(key)
            break
# Items the sheet does not have yet go after the last row.
last = max(i for i, l in enumerate(lines) if l.startswith('| '))
extra = [f'| {k} | {v[0]} | {v[1].replace("|", "/")} |' for k, v in verdicts.items() if k not in used]
lines[last + 1:last + 1] = extra
lines = [l.replace('Reviewed by: TODO', f'Reviewed by: {reviewer}') for l in lines]
open(sheet, 'w', encoding='utf-8', newline='').write(nl.join(lines))
print(f'{sheet}: {len(used)} rows set, {len(extra)} rows added')
