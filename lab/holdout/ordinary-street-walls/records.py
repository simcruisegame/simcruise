"""Crop transparent texels, preserve edge gutters and write the two static runtime records."""

from planes import Site, D, np, cv2, Image, Path, sys, json
import hashlib
target = Path(sys.argv[1])
target.mkdir(parents=True, exist_ok=True)
for site in ('iowa', 'minneapolis'):
    s = Site(site)
    d = json.loads((D / (site + '-wall.json')).read_text())
    tex = np.array(Image.open(d['texture']).convert('RGBA'))
    h, w = tex.shape[:2]
    yy, xx = np.where(tex[:, :, 3] > 0)
    x0 = max(0, int(xx.min()) - 2)
    x1 = min(w, int(xx.max()) + 3)
    y0 = max(0, int(yy.min()) - 2)
    y1 = min(h, int(yy.max()) + 3)
    near = cv2.dilate(tex[:, :, 3], np.ones((5, 5), 'B')) > 0
    tex[~near, :3] = 0
    img = Image.fromarray(tex[y0:y1, x0:x1])
    png = 'survey-wall-' + site + '.png'
    img.save(target / png, optimize=True)
    b = s.tile['buildings'][d['bi']]
    f = list(s.frames)
    record = {'schema': 1, 'tile': s.tile['id'], 'center': b[4:6], 'ring': b[0][0], 'edge': d['ei'], 'ground_uv': d['ground_uv'], 'normal': d['normal'], 'u': [x0 / w, x1 / w], 'z_m': [d['height'] * (1 - y1 / h), d['height'] * (1 - y0 / h)], 'file': png, 'sha256': hashlib.sha256((target / png).read_bytes()).hexdigest(), 'credit': f"{f[0]['author']} / Panoramax · CC BY-SA 4.0", 'training_sources': [Q['credit_url'] for Q in f[:2]], 'license': 'https://creativecommons.org/licenses/by-sa/4.0/'}
    jsonname = 'survey-wall-%d-%d.json' % tuple(s.tile['id'])
    (target / jsonname).write_text(json.dumps(record, separators=(',', ':')) + '\n')
    (target / (png + '.import')).write_text('[remap]\n\nimporter="keep"\n')
    print(site, png, (target / png).stat().st_size, jsonname, (target / jsonname).stat().st_size, record['u'], record['z_m'])
