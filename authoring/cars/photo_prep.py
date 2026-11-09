"""Prepare photographic textures for build_car.py (the Miata method): crop a
head-on Commons photograph of the reference car, paint out manufacturer
badges (OpenCV inpainting) and save it next to the designs.

    python3 authoring/cars/photo_prep.py SOURCE.jpg OUT.jpg x0 y0 x1 y1 [--emit MASK.jpg boxes...] [--recolor HEX [--keep boxes...]] [--badge bx0 by0 bx1 by1 ...]

--recolor replaces pale, unsaturated pixels (white or silver paint) with the
model's paint colour, so a photo of a differently painted car can be used.

The crop box and badge boxes are pixel coordinates in SOURCE. The design file
maps car coordinates to pixels of OUT (see "photo" materials in features.py).
Needs NumPy, OpenCV (cv2) and Pillow; not needed to build the cars.
"""
import sys
import cv2
import numpy as np

a = sys.argv[1:]
src, out = a[0], a[1]
x0, y0, x1, y1 = map(int, a[2:6])
img = cv2.imread(src)
badges = []
if "--badge" in a:
    v = list(map(int, a[a.index("--badge") + 1:]))
    badges = [v[i:i + 4] for i in range(0, len(v), 4)]
mask = np.zeros(img.shape[:2], np.uint8)
for bx0, by0, bx1, by1 in badges:  # noqa
    mask[by0:by1, bx0:bx1] = 255
if badges:
    img = cv2.inpaint(img, mask, 9, cv2.INPAINT_TELEA)
if "--emit" in a:
    # brake-lamp mask: saturated red pixels inside the given boxes (before any recolour)
    i = a.index("--emit"); mpath = a[i + 1]
    k = []
    for t in a[i + 2:]:
        if t.startswith("--"):
            break
        k.append(int(t))
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    red = (((hsv[..., 0] < 10) | (hsv[..., 0] > 165)) & (hsv[..., 1] > 110) & (hsv[..., 2] > 60)).astype(np.uint8) * 255
    m = np.zeros_like(red)
    for j in range(0, len(k), 4):
        m[k[j + 1]:k[j + 3], k[j]:k[j + 2]] = red[k[j + 1]:k[j + 3], k[j]:k[j + 2]]
    m = cv2.GaussianBlur(m, (5, 5), 0)
    mf = m.astype(float) / 255   # red light where the lens is red
    cv2.imwrite(mpath, (np.dstack([mf * 30, mf * 45, mf * 255]))[y0:y1, x0:x1].astype(np.uint8), [cv2.IMWRITE_JPEG_QUALITY, 92])
if "--recolor" in a:
    h = a[a.index("--recolor") + 1].lstrip("#")
    rgb = np.array([int(h[i:i + 2], 16) for i in (4, 2, 0)], float)       # BGR
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV).astype(float)
    pale = np.clip((0.22 - hsv[..., 1] / 255) / 0.08, 0, 1) * np.clip((hsv[..., 2] / 255 - 0.5) / 0.15, 0, 1)
    shade = np.clip(hsv[..., 2] / 235.0, 0.6, 1.0)[..., None]
    if "--keep" in a:   # chrome and other pale parts that are not paint
        k = []
        for t in a[a.index("--keep") + 1:]:
            if t.startswith("--"):
                break
            k.append(int(t))
        for i in range(0, len(k), 4):
            pale[k[i + 1]:k[i + 3], k[i]:k[i + 2]] = 0
    img = (img * (1 - pale[..., None]) + rgb * shade * pale[..., None]).astype(np.uint8)
cv2.imwrite(out, img[y0:y1, x0:x1], [cv2.IMWRITE_JPEG_QUALITY, 92])
print(out, x1 - x0, y1 - y0)
