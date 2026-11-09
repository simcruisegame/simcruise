"""Build a depth-tested facade atlas from licensed cameras, control points and source masks."""

import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter
from scipy.ndimage import binary_opening, distance_transform_edt, gaussian_filter, map_coordinates, maximum_filter, minimum_filter


def project(camera, points):
    delta = points - camera['origin']
    depth = delta @ camera['forward']
    image = np.column_stack((delta @ camera['right'], -(delta @ camera['up'])))
    return image / depth[:, None] * camera['focal'] + camera['principal'], depth


def supported_spans(site, max_error_m=1.25):
    """Both neighboring physical stations must be registered in two independent views."""
    support = {i: [] for i in site['stations']}
    stations = {}
    direction = np.r_[site['long_axis'], 0.]
    evidence = {}
    for camera in site['sources']:
        controls = [p for p in camera['controls'] if p['station'] is not None]
        xyz = np.asarray([p['world'] for p in controls])
        observed = [p['image'] for p in controls]
        predicted, _ = project(camera, xyz)
        nearby, _ = project(camera, xyz + direction)
        scale = np.linalg.norm(nearby - predicted, axis=1)
        errors = np.linalg.norm(predicted - observed, axis=1) / np.maximum(scale, .001)
        report = {}
        for row, error in zip(controls, errors):
            station = row['station']
            stations[station] = float((np.asarray(row['world'][:2]) - site['center']) @ direction[:2])
            report[str(station)] = round(float(error), 3)
            if error <= max_error_m:
                support[station].append(camera['name'])
        evidence[camera['name']] = report
    pairs = zip(site['stations'][:-1], site['stations'][1:])
    spans = [(stations[a], stations[b]) for a, b in pairs if min(len(support[a]), len(support[b])) >= 2]
    return spans, evidence


def triangles(profile, side):
    vertices = np.asarray(profile['v']) * [side / 100000, side / 100000, .01]
    faces = list(vertices[np.asarray(profile['f']).reshape(-1, 3)])
    def wall(a, b, top_a, top_b, low):
        at, bt = np.r_[a, top_a], np.r_[b, top_b]
        ab, bb = np.r_[a, low], np.r_[b, low]
        faces.extend([[ab, bb, bt], [ab, bt, at]])
    for ring in profile['footprint']:
        xy = np.asarray(ring) * side / 100000
        for a, b in zip(xy, np.roll(xy, -1, axis=0)):
            wall(a, b, profile['eave_m'], profile['eave_m'], -1.3)
    for i, j in np.asarray(profile['edge']).reshape(-1, 2):
        a, b = vertices[i], vertices[j]
        wall(a[:2], b[:2], a[2], b[2], profile['eave_m'])
    return np.asarray(faces)


def depth_buffer(camera, mesh):
    width, height = camera['size']
    depths = np.full((height, width), np.inf, np.float32)
    for triangle in mesh:
        uv, z = project(camera, triangle)
        if np.any(z <= .1):
            continue
        lo = np.maximum(0, np.floor(uv.min(axis=0)).astype(int))
        hi = np.minimum([width-1, height-1], np.ceil(uv.max(axis=0)).astype(int))
        if np.any(lo > hi):
            continue
        x, y = np.meshgrid(np.arange(lo[0], hi[0]+1)+.5, np.arange(lo[1], hi[1]+1)+.5)
        a, b, c = uv
        denominator = (b[1]-c[1])*(a[0]-c[0]) + (c[0]-b[0])*(a[1]-c[1])
        if abs(denominator) <= .0001:
            continue
        w0 = ((b[1]-c[1])*(x-c[0]) + (c[0]-b[0])*(y-c[1])) / denominator
        w1 = ((c[1]-a[1])*(x-c[0]) + (a[0]-c[0])*(y-c[1])) / denominator
        w2 = 1-w0-w1
        inside = np.minimum(np.minimum(w0, w1), w2) >= -.0001
        interpolated = 1/(w0/z[0] + w1/z[1] + w2/z[2])
        region = depths[lo[1]:hi[1]+1, lo[0]:hi[0]+1]
        np.minimum(region, np.where(inside, interpolated, np.inf), out=region)
    depths[~np.isfinite(depths)] = 0
    return depths


def sample(image, coordinates, order=1):
    return map_coordinates(image, coordinates, order=order, mode='constant', cval=0)


def observations(camera, mesh, xyz, directory, photos):
    source_file = photos / camera['file']
    if hashlib.sha256(source_file.read_bytes()).hexdigest() != camera['sha256']:
        raise ValueError(f"Photograph checksum mismatch for {camera['name']}")
    photo = Image.open(source_file).convert('RGB').resize(camera['size'], Image.Resampling.LANCZOS)
    rgb = np.asarray(photo, np.float32)
    r, g, b = rgb.transpose(2, 0, 1)
    mask = Image.open(directory / f"{camera['name']}-visible.png").convert('L')
    if list(mask.size) != list(camera['size']):
        raise ValueError('Source mask and registered camera size differ')
    clear = np.array(mask.filter(ImageFilter.MinFilter(5))) > 245
    foliage = (g > r * 1.15) & (g > b * 1.1)
    sky = (b - r > 22) & (b > g * 1.09)
    clear &= ~sky & ~foliage
    uv, depth = project(camera, xyz)
    coords = (uv[:, 1] - .5, uv[:, 0] - .5)
    visible = sample(clear.astype(np.uint8), coords, 0) > 0
    visible &= abs(sample(depth_buffer(camera, mesh), coords, 0) - depth) < .45
    colors = np.stack([sample(channel, coords) for channel in (r, g, b)], axis=1)
    return colors, visible


def match_exposure(colors, valid, reference, ref_valid):
    shared = valid & ref_valid & (colors.mean(axis=1) > 65) & (reference.mean(axis=1) > 75)
    if shared.sum() < 100:
        raise ValueError('Insufficient shared facade for exposure registration')
    corrected = colors.copy()
    for channel in range(3):
        x = np.percentile(colors[shared, channel], [10, 50, 90])
        y = np.percentile(reference[shared, channel], [10, 50, 90])
        knots = np.r_[0, x, 255]
        if np.all(np.diff(knots) > 1):
            corrected[:, channel] = np.interp(colors[:, channel], knots, np.r_[0, y, 255])
    return corrected


def build(site_path, geometry_path, photos, output):
    site = json.loads(site_path.read_text())
    document = json.loads(geometry_path.read_text())
    profile = document['roofs'][0]
    mesh = triangles(profile, site['side_m'])
    spans, residuals = supported_spans(site)
    # Use a bounded virtual camera to cap memory and live texture size.
    master = dict(site['sources'][0])
    master.update(size=[1440, 810], focal=master['focal']*1.35,
                  principal=np.asarray(master['principal'])*1.35 + [72., 0.])
    depths = depth_buffer(master, mesh)
    y, x = np.nonzero(depths)
    rays = (np.asarray(master['forward']) + ((x+.5-master['principal'][0])/master['focal'])[:, None]*master['right']
            - ((y+.5-master['principal'][1])/master['focal'])[:, None]*master['up'])
    xyz = master['origin'] + rays*depths[y, x, None]
    station = (xyz[:, :2]-site['center']) @ site['long_axis']
    eligible = np.zeros(len(station), bool)
    for low, high in spans:
        eligible |= (station >= low) & (station <= high)
    samples = [observations(c, mesh, xyz, site_path.parent, photos) for c in site['sources']]
    # A stable stone/window sample must survive two masks and pass an independent
    # luminance and chroma check. A color from one photograph alone is insufficient.
    ref = site['reference_source']
    colors = [match_exposure(c, v, *samples[ref]) if i != ref else c for i, (c, v) in enumerate(samples)]
    # Sub-metre control residuals displace sharp window edges between photos.
    # Check coarse appearance and a bounded range around each projected edge.
    # Never blur the actual color selected from the first corroborated photograph.
    consensus = []
    for raster, (_, visible) in zip(colors, samples):
        image = np.zeros((*depths.shape, 3), np.float32)
        image[y[visible], x[visible]] = raster[visible]
        weights = np.zeros(depths.shape, np.float32)
        weights[y[visible], x[visible]] = 1.
        denom = np.maximum(gaussian_filter(weights, 5.), .001)
        smooth = np.stack([gaussian_filter(image[:, :, j], 5.)/denom for j in range(3)], axis=2)
        lum = image @ np.array([.2126, .7152, .0722], np.float32)
        low = minimum_filter(np.where(weights > 0, lum, 255.), 11)
        high = maximum_filter(np.where(weights > 0, lum, 0.), 11)
        consensus.append((smooth[y,x], low[y,x], high[y,x]))
    chosen = np.zeros((len(x), 3), np.float32)
    weight = np.zeros(len(x), np.float32)
    for i, (a, (_, va)) in enumerate(zip(colors, samples)):
        for j in range(i+1, len(colors)):
            b, vb = colors[j], samples[j][1]
            coarse = consensus[i][0] - consensus[j][0]
            light = np.abs(coarse @ [.2126, .7152, .0722])
            chroma = np.max(np.abs(coarse-coarse.mean(axis=1)[:, None]), axis=1)
            luminance = a @ [.2126, .7152, .0722]
            edge_agrees = (luminance >= consensus[j][1]-35.) & (luminance <= consensus[j][2]+35.)
            good = eligible & va & vb & (light <= 56.) & (chroma <= 24.) & edge_agrees
            first = good & (weight == 0)
            chosen[first] = a[first]
            weight[good] += 1
    accepted = np.zeros(depths.shape, bool)
    accepted[y, x] = weight > 0
    # Remove isolated agreements (including thin branches). Taper only at mask edges.
    accepted = binary_opening(accepted, structure=np.ones((3, 3), bool))
    strength = np.clip(distance_transform_edt(accepted)/2.5, 0, 1)
    rgba = np.zeros((*depths.shape, 4), np.uint8)
    rgba[y, x, :3] = np.clip(chosen, 0, 255).astype(np.uint8)
    rgba[:, :, 3] = np.rint(strength*255).astype(np.uint8)
    rgba[~accepted, :3] = 0
    if accepted.sum() < 1000:
        raise ValueError('Insufficient corroborated photographic surface')
    ay, ax = np.nonzero(accepted)
    x0, x1 = max(0,ax.min()-3), min(depths.shape[1],ax.max()+4)
    y0, y1 = max(0,ay.min()-3), min(depths.shape[0],ay.max()+4)
    crop = np.s_[y0:y1, x0:x1]
    out = output
    out.mkdir(parents=True, exist_ok=True)
    tile = '-'.join(map(str, site['tile']))
    color_file = f'survey-front-{tile}.png'
    depth_file = f'survey-depth-{tile}.png'
    # Extend edge color through transparent texels so the mip chain stays clean.
    _, nearest = distance_transform_edt(~accepted[crop], return_indices=True)
    image = rgba[crop].copy()
    image[:, :, :3] = image[nearest[0], nearest[1], :3]
    Image.fromarray(image).save(out/color_file, optimize=True)
    cm = np.rint(depths[crop]*100)
    if cm.max() > 65535:
        raise ValueError('Projection exceeds 16-bit centimetre range')
    cm = cm.astype(np.uint16)
    rg = np.zeros((*cm.shape,3), np.uint8)
    rg[:, :, 0], rg[:, :, 1] = cm>>8, cm&255
    Image.fromarray(rg).save(out/depth_file, optimize=True)
    def engine(v):
        return np.round(np.asarray(v)[[0,2,1]], 8).tolist()
    profile['projection'] = dict(file=color_file, depth=depth_file, size=list(image.shape[:2][::-1]),
                                 focal=master['focal'], principal=(master['principal']-[x0,y0]).tolist(),
                                 camera=engine(master['origin']), right=engine(master['right']),
                                 up=engine(master['up']), forward=engine(master['forward']),
                                 wall_srgb=site['wall_srgb'])
    profile['projection']['size'] = [int(v) for v in profile['projection']['size']]
    (out/f'lidar-roofs-{tile}.json').write_text(json.dumps(document,separators=(',',':'))+'\n')
    return dict(supported_spans_m=spans, station_residuals_m=residuals, confirmed_pixels=int(accepted.sum()),
                masked_east_pixels=int(np.sum(accepted[y,x] & (station>spans[-1][1]))))
