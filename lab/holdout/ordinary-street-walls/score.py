"""Compare native material A/B images against the third photograph on visible static wall pixels."""

from planes import D, np, cv2, Image, ImageDraw, json
out = {}
pil = []
for slug in ['iowa', 'minneapolis']:
    i = lambda ending: np.array(Image.open(D / (slug + '-' + ending + '.png')).convert('RGB'))
    ref = i('withheld')
    old = i('native-control')
    new = i('native-survey')
    probe = i('native-mask')
    repeat = i('native-repeat')
    p = probe.astype('f')
    mask = (p[:, :, 0] > p[:, :, 1] * 1.8 + 20) & (p[:, :, 2] > p[:, :, 1] * 1.8 + 20) & (p[:, :, 0] > 105) & (p[:, :, 2] > 105)
    dmask = np.max(abs(old.astype('f') - new), axis=2) > 3
    mask &= np.max(abs(p - old), axis=2) > 30
    stat = i('static-held')[:, :, 0] > 170
    keep = mask & stat
    if slug == 'iowa':
        occ = np.zeros(keep.shape, 'B')
        for poly in [[(385, 100), (406, 101), (390, 603), (363, 603)], [(349, 232), (393, 220), (389, 358), (340, 366)], [(391, 205), (431, 199), (437, 346), (388, 356)], [(388, 139), (448, 112), (470, 107), (472, 123), (446, 131), (390, 152)]]:
            cv2.fillPoly(occ, [np.array(poly, 'i')], 255)
        keep &= occ == 0

    def metrics(im):
        a = cv2.GaussianBlur(im, (0, 0), 1.7)
        b = cv2.GaussianBlur(ref, (0, 0), 1.7)
        v = cv2.cvtColor(a, cv2.COLOR_RGB2GRAY).astype('f')
        u = cv2.cvtColor(b, cv2.COLOR_RGB2GRAY).astype('f')
        ncc = lambda p, q: round(float(np.corrcoef(p[keep], q[keep])[0, 1]), 3)
        y1 = v - cv2.GaussianBlur(v, (0, 0), 13)
        y2 = u - cv2.GaussianBlur(u, (0, 0), 13)
        return {'rgb_mae': round(float(np.mean(abs(a.astype('f')[keep] - b[keep]))), 2), 'gray_correlation': ncc(u, v), 'detail_correlation': ncc(y1, y2)}
    ob = {'control_repeat_mean': float(np.mean(abs(old.astype('f') - repeat))), 'control_repeat_changed': int(np.sum(np.max(abs(old.astype('f') - repeat), axis=2) > 3)), 'changed': int(sum(dmask.ravel())), 'rendered_mask': int(mask.sum()), 'heldout_static_pixels': int(keep.sum()), 'control': metrics(old), 'survey': metrics(new)}
    print(slug, ob, flush=True)
    out[slug] = ob
    for img, txt in [(ref, slug + ' · withheld Panoramax photo'), (old, 'Godot · existing material'), (new, 'Godot · observed wall finish'), (probe, 'Godot · sample extent')]:
        view = Image.fromarray(img).resize((360, 240), Image.Resampling.LANCZOS)
        pil.append((txt, view))
sh = Image.new('RGB', (1440, 534), '#1d2023')
d = ImageDraw.Draw(sh)
for n, (name, m) in enumerate(pil):
    x = n % 4 * 360
    y = n // 4 * 267
    sh.paste(m, (x, y + 22))
    d.text((x + 5, y + 4), name)
sh.save(D / 'heldout-comparison.jpg', quality=91)
(D / 'score.json').write_text(json.dumps(out, indent=2))
