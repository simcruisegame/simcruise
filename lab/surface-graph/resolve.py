"""Prototype: the place knowledge graph chooses the surfaces of one tile.

Facts about each road, land patch and building (class, land cover, region, climate,
season, style) run through the graph's rules (data/knowledge/place-graph.json) and the
surface rules here (surface-rules.json), with the same engine: weakest 'if' fact,
'unless' facts lower it, several rules add by noisy OR, negative weights suppress.
Measured facts from the unbaked aerial photograph (data/tiles/aerial/unbaked) are added
as evidence, and the measured colour of each patch is reported next to the rule's colour.

    python lab/surface-graph/resolve.py 18180 27907 --month 11
"""
import argparse, json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
ap = argparse.ArgumentParser(); ap.add_argument('x', type=int); ap.add_argument('y', type=int)
ap.add_argument('--month', type=int, default=11); ap.add_argument('--out', default='.')
a = ap.parse_args()
graph = json.load(open(os.path.join(ROOT, 'data/knowledge/place-graph.json')))
surf = json.load(open(os.path.join(os.path.dirname(__file__), 'surface-rules.json')))
tile = json.load(open(os.path.join(ROOT, f'data/tiles/{a.x}_{a.y}.json')))
RULES = graph['rules'] + surf['rules']; MAT = surf['materials']

def lonlat(u, v):
    n = 65536; X, Y = (a.x + u) / n, (a.y + v) / n
    return X * 360 - 180, math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * Y))))

def infer(facts):
    facts = dict(facts)
    for _ in range(6):
        add = {}
        for r in RULES:
            if not all(f in facts for f in r['if']): continue
            c = min(facts[f] for f in r['if'])
            for u in r.get('unless', []): c *= 1 - facts.get(u, 0)
            for k, w in r['then'].items(): add.setdefault(k, []).append(c * w)
        changed = False
        for k, ws in add.items():
            pos = 1 - np.prod([1 - w for w in ws if w > 0]); neg = 1 - np.prod([1 + w for w in ws if w < 0])
            v = max(0.0, pos * (1 - neg))
            if k not in facts or abs(facts[k] - v) > 1e-6 and not k.startswith(('land:', 'road:', 'region:', 'climate:', 'measured:', 'season:', 'form:', 'district:', 'near:')):
                if facts.get(k) != v: facts[k] = v; changed = True
        if not changed: break
    return facts

lon, lat = lonlat(0.5, 0.5)
base = {}
for reg in graph['regions']:
    s, w, n, e = reg['box']
    if s <= lat <= n and w <= lon <= e: base.update(reg['facts'])
clim = [k for k in base if k.startswith('climate:')]
m = a.month
base['season:' + ('spring' if m in (3, 4, 5) else 'summer' if m in (6, 7, 8) else 'autumn' if m in (9, 10, 11) else 'winter')] = 1.0
if 'climate:humid_subtropical' in base and m in (11, 12, 1, 2, 3): base['season:dormant_warm'] = 0.9
if any(l[0] == 'water' and l[1] in ('ocean', 'sea', 'bay') for l in tile['land']): base['near:sea'] = 1.0

# The unbaked aerial photograph (prototype 1) as measurement.
key = tile.get('aerial_key', ''); photo = None
for p in (f'data/tiles/aerial/unbaked/{key}', f'data/tiles/aerial/{key}'):
    if key and os.path.exists(os.path.join(ROOT, p)):
        photo = np.asarray(Image.open(os.path.join(ROOT, p)).convert('RGB')).astype(float); src = p; break
qx, qy = a.x % 4, a.y % 4
def px(u, v):
    N = photo.shape[0]; return (qx + u) * N / 4, (qy + v) * N / 4
def mean_in(mask_draw):
    N = photo.shape[0]; im = Image.new('L', (N, N)); mask_draw(ImageDraw.Draw(im)); mk = np.asarray(im) > 0
    return photo[mk].mean(0) if mk.sum() >= 4 else None

def best(facts, prefix):
    c = [(v, k) for k, v in facts.items() if k.startswith(prefix) and k in MAT and v > 0.05]
    return max(c) if c else None

plan = {'tile': [a.x, a.y], 'lonlat': [round(lon, 5), round(lat, 5)], 'month': m, 'base_facts': base, 'photo': src if photo is not None else None, 'features': []}
ROADMAP = {'minor': 'residential'}
for kind, items in (('land', tile['land']), ('road', tile['roads'])):
    for it in items:
        if kind == 'land':
            if it[0] == 'water': continue
            f = dict(base); f['land:' + it[1]] = 1.0
            rings = it[2]
            meas = mean_in(lambda d: [d.polygon([px(*p) for p in ring], fill=1) for ring in rings[:1]]) if photo is not None else None
        else:
            cls = ROADMAP.get(it[0], it[0]); f = dict(base); f['road:' + cls] = 1.0
            if len(it) > 5 and it[5] in ('concrete', 'chipseal'): f['road:surface_' + it[5]] = 1.0
            wpx = max(1, it[4] / 610 * photo.shape[0] / 4) if photo is not None else 1
            meas = mean_in(lambda d: d.line([px(*p) for p in it[3]], fill=1, width=int(round(wpx)))) if photo is not None else None
            if meas is not None and meas.mean() > 120: f['measured:bright_pavement'] = 0.7
        out = infer(f)
        pick = best(out, 'surface:')
        if not pick: continue
        mat = MAT[pick[1]]; col = np.array(mat.get('albedo_srgb', [128, 128, 128]), float)
        for st in [k for k in out if k.startswith('state:') and k in MAT and out[k] > 0.3]:
            t = MAT[st]; col = col * (1 - t['tint_amount']) + np.array(t['tint_srgb']) * t['tint_amount']
        why = [r.get('why', '') for r in RULES if r.get('why') and all(x in out for x in r['if']) and pick[1] in r['then']]
        plan['features'].append({'kind': kind, 'what': it[1] if kind == 'land' else f"{it[0]} {it[2]}".strip(),
            'surface': pick[1], 'confidence': round(pick[0], 2), 'states': [k for k in out if k.startswith(('state:', 'wear:')) and out[k] > 0.3],
            'rule_colour': [int(c) for c in col], 'measured_colour': None if meas is None else [int(c) for c in meas],
            'why': why[:2], 'geom': it[2] if kind == 'land' else it[3], 'width': None if kind == 'land' else it[4]})

os.makedirs(a.out, exist_ok=True)
name = f'surface-plan-{a.x}-{a.y}'
json.dump({k: v for k, v in plan.items()} | {'features': [{k: v for k, v in f.items() if k not in ('geom', 'width')} for f in plan['features']]},
          open(os.path.join(a.out, name + '.json'), 'w'), indent=1)
# Picture: photo | the graph's choice (rule colour) | the graph's choice corrected by measurement
S = 512
def draw(use):
    im = Image.new('RGB', (S, S), (55, 55, 58)); d = ImageDraw.Draw(im)
    for f in sorted(plan['features'], key=lambda f: f['kind'] != 'land'):
        c = f['rule_colour'] if use == 'rule' or f['measured_colour'] is None else [int(0.5 * r + 0.5 * mm) for r, mm in zip(f['rule_colour'], f['measured_colour'])]
        if f['kind'] == 'land': d.polygon([(u * S, v * S) for u, v in f['geom'][0]], fill=tuple(c))
        else: d.line([(u * S, v * S) for u, v in f['geom']], fill=tuple(c), width=max(2, int(f['width'] / 610 * S)))
    return im
N = photo.shape[0]; crop = Image.fromarray(photo[qy * N // 4:(qy + 1) * N // 4, qx * N // 4:(qx + 1) * N // 4].astype(np.uint8)).resize((S, S))
sheet = Image.new('RGB', (3 * S + 40, S), 'white')
for i, im in enumerate((crop, draw('rule'), draw('blend'))): sheet.paste(im, (i * (S + 20), 0))
sheet.save(os.path.join(a.out, name + '.png'))
from collections import Counter
print(f'{name}: {len(plan["features"])} features, facts {sorted(base)}')
for (k, s), n in Counter((f['kind'], f['surface']) for f in plan['features']).most_common():
    ex = next(f for f in plan['features'] if f['surface'] == s)
    print(f'  {n:3d} {k:4s} -> {s} (conf {ex["confidence"]}, states {ex["states"]}); rule colour {ex["rule_colour"]}, measured {ex["measured_colour"]}')
