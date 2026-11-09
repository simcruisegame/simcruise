"""Limit rectified texture to regions supported by two views and competing depth hypotheses."""

import numpy as np
import cv2

def supported_surface(s, physical, heights, side, source_a, source_b, semantic, hull):
    pa = s.pos[0]
    pb = s.pos[1]
    r = physical - pa
    dist = (physical[0] - pa) @ side
    up = heights[:, None] - 2.3

    def error(first, second):
        x = cv2.cvtColor(first, cv2.COLOR_RGB2GRAY).astype('f')
        y = cv2.cvtColor(second, cv2.COLOR_RGB2GRAY).astype('f')
        return cv2.GaussianBlur(abs(x - cv2.GaussianBlur(x, (0, 0), 6) - (y - cv2.GaussianBlur(y, (0, 0), 6))), (0, 0), 5)
    err0 = error(source_a, source_b)
    alternatives = []
    for dd in (-4.0, 4.0):
        f = (dist + dd) / dist
        q = pa + r * f - pb
        other = s.project(1, q[:, 0], q[:, 1], up * f)
        alternatives.append(error(source_a, other))
    alternate = np.minimum(*alternatives)
    rgb_difference = cv2.blur(np.mean(abs(source_a.astype('f') - source_b), axis=2), (9, 9))
    near = cv2.distanceTransform((hull == 0).astype('B'), cv2.DIST_L2, 3) < 54
    confident = (alternate > err0 * 1.22 + 1.5) & (err0 < 14) & (rgb_difference < 23) & near
    valid = semantic & (heights[:, None] > 3.2) & (rgb_difference < 28)
    mask = (valid & ((hull > 0) | confident)).astype('B')
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, np.ones((5, 5), 'B'))
    total, labels, stats, _ = cv2.connectedComponentsWithStats(mask)
    for i in range(1, total):
        region = labels == i
        if np.sum(region & (hull > 0)) < 7:
            mask[region] = 0
    depth = cv2.distanceTransform(mask, cv2.DIST_L2, 5)
    feather = max(4, min(29, np.percentile(depth[depth > 0], 92)))
    f = np.clip(depth / feather, 0, 1)
    return (mask, f * f * (3 - 2 * f), dict(in_hull=int((hull > 0).sum()), dense_pixels=int((mask > 0).sum()), feather_texels=float(feather)))
