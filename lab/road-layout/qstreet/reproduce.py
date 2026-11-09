"""Rebuild registered Q Street paint and compare it to the bundled game file."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
from urllib.request import Request, urlopen

args = argparse.ArgumentParser()
args.add_argument('work', type=Path, help='A dedicated, writable working directory')
options = args.parse_args()
root = Path(__file__).resolve().parent
work = options.work.resolve()
work.mkdir(parents=True, exist_ok=True)
os.environ['SIMCRUISE_LAYOUT_WORK'] = str(work)

for row in json.loads((root/'sources.json').read_text()):
    file = work/row['path']
    if not file.is_file():
        request = Request(row['url'], headers={'User-Agent':'SundayDriver-road-source-check/1.0'})
        with urlopen(request, timeout=15) as response:
            data = response.read(row['bytes']+1)
        if len(data) != row['bytes'] or hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Source edition changed: '+row['path'])
        file.write_bytes(data)
    if hashlib.sha256(file.read_bytes()).hexdigest() != row['sha256']:
        raise ValueError('Source hash mismatch: '+row['path'])
for file in ['dc-aerial.json','dc2023.json','Washington-roads.json']:
    shutil.copyfile(root/file,work/file)
for file in ['dc-game.json','dc-production.json']:
    shutil.copyfile(root/'base-roads.json',work/file)
from curbs import compare
from fit_mark import build
from prepare_bay import prepare

compare()
build()
prepare()
import controls
actual=json.loads((work/'q-registered.json').read_text())
bundled=json.loads((root.parents[2]/'game/data/road-layout-18745-25067.json').read_text())
if actual != bundled:
    sys.exit('Source reconstruction differs from the bundled survey')
print('Verified the bundled geometry and all displaced/conflicting controls.')
