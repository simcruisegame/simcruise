// Tree crowns from a canopy height grid by classic segmentation (no learned weights):
// treetops are local maxima in a window that grows with the tree's height (Popescu and
// Wynne 2004, crown width 2.5 + 0.009 h^2 m), and each top grows its crown by region
// growing, highest pixel first (Dalponte and Coomes 2016): a pixel joins a crown while it
// is canopy (2 m or more), at least 45 % of the top's height, at least 55 % of the
// crown's mean height so far, downhill from it, and within the largest crown radius for that
// height. Two more passes find the trees a tall top's window hid in the canopy left over.
// On the Great Smokies tile 17487/25850 (CHMv2): 692 crowns covering 80 % of the canopy,
// median width 15.4 m at 0.77 of the height (the old peak finder: 494 at 0.98), 0.66 s.
//
// grid: row-major heights in metres (the CHMv2 zoom-16 block: 512 x 512, north row first).
// side: the grid's side in metres. Returns crowns with the tile-UV top, height and width
// (as canopy.mjs rows) and the crown's shape: radius in 8 directions (east first,
// anticlockwise, metres), the crown base height, and the crown centre's offset from the
// top (UV), which a leaning or one-sided crown has.

const CANOPY_M = 2, MIN_TOP_M = 3, SEED_SHARE = .45, MEAN_SHARE = .55;

/** The crown width a tree of height h usually has (m), from the window rule. */
export function crownWidthFor(h) { return 2.5 + .009 * h * h; }

/**
 * The largest crown radius a top of height h may grow to (m): half its height (a broadleaf's
 * crown can be as wide as the tree is tall), 3 to 15 m. Only a bound: crowns end at the valleys
 * between them and at 45 % of their top. (The window rule's width is too narrow for this: on a
 * closed broadleaf canopy, the Great Smokies, it left half the canopy outside every crown.)
 */
export function maxCrownRadiusFor(h) { return Math.min(15, Math.max(3, .5 * h)); }

function smooth(grid, n) {
 // 3 x 3 binomial filter (1 2 1), clamped at the edges; canopy under 2 m is ground.
 const out = new Float32Array(n * n), w = [1, 2, 1];
 for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
  let s = 0, ws = 0;
  for (let j = -1; j <= 1; j++) for (let i = -1; i <= 1; i++) {
   const yy = y + j, xx = x + i;
   if (yy < 0 || yy >= n || xx < 0 || xx >= n) continue;
   const k = w[i + 1] * w[j + 1];
   s += k * grid[yy * n + xx]; ws += k;
  }
  out[y * n + x] = s / ws;
 }
 return out;
}

// Treetops: local maxima in the height-scaled window. With label (a later pass), only pixels
// no crown took yet can be tops and only they suppress one another in the window, but a top
// must still be a local maximum of its 3 x 3 neighbours, taken or not: a crown's skirt (below
// its 45 %, left over) slopes up into the crown and never makes a ring of small trees.
function tops(h, n, cell, label = null) {
 const found = [];
 for (let y = 1; y < n - 1; y++) for (let x = 1; x < n - 1; x++) {
  const i = y * n + x, v = h[i];
  if (v < MIN_TOP_M || (label && label[i] >= 0)) continue;
  // the 3 x 3 test first (cheap); the height-scaled window only for those left
  if (v < h[i - 1] || v < h[i + 1] || v < h[i - n] || v < h[i + n] || v < h[i - n - 1] || v < h[i - n + 1] || v < h[i + n - 1] || v < h[i + n + 1]) continue;
  const r = Math.max(1, .5 * crownWidthFor(v) / cell), R = Math.ceil(r), r2 = r * r;
  let top = true;
  for (let j = -R; j <= R && top; j++) for (let k = -R; k <= R; k++) {
   if (j * j + k * k > r2 || (j === 0 && k === 0)) continue;
   const yy = y + j, xx = x + k;
   if (yy < 0 || yy >= n || xx < 0 || xx >= n) continue;
   const oi = yy * n + xx, o = h[oi];
   if (label && label[oi] >= 0 && (j * j > 1 || k * k > 1)) continue;
   // a plateau keeps one top: the first pixel in row order
   if (o > v || (o === v && oi < i)) { top = false; break; }
  }
  if (top) found.push(i);
 }
 return found;
}

// A binary max-heap of pixels for the region growing. Key: the pixel's smoothed height less
// 0.005 x its reach (the distance from the crown's top over the crown's largest radius).
// Smoothed heights are sixteenths, ninths or twelfths of whole metres, so two heights differ by
// 1/144 m or more and the reach only orders pixels of one height: a valley between two crowns
// splits between them by nearness. Equal keys go by pixel, then crown, so the result never
// depends on push order. Typed arrays: a dense tile pushes about two million entries.
class Heap {
 constructor(capacity) { this.key = new Float64Array(capacity); this.pix = new Int32Array(capacity); this.crown = new Int32Array(capacity); this.size = 0; }
 above(i, j) { const a = this.key[i], b = this.key[j]; return a > b || (a === b && (this.pix[i] < this.pix[j] || (this.pix[i] === this.pix[j] && this.crown[i] < this.crown[j]))); }
 swap(i, j) {
  const k = this.key[i]; this.key[i] = this.key[j]; this.key[j] = k;
  const p = this.pix[i]; this.pix[i] = this.pix[j]; this.pix[j] = p;
  const c = this.crown[i]; this.crown[i] = this.crown[j]; this.crown[j] = c;
 }
 push(key, pix, crown) {
  if (this.size === this.key.length) {
   const grow = (a, T) => { const b = new T(2 * a.length); b.set(a); return b; };
   this.key = grow(this.key, Float64Array); this.pix = grow(this.pix, Int32Array); this.crown = grow(this.crown, Int32Array);
  }
  let i = this.size++;
  this.key[i] = key; this.pix[i] = pix; this.crown[i] = crown;
  while (i > 0) { const p = (i - 1) >> 1; if (!this.above(i, p)) break; this.swap(i, p); i = p; }
 }
 // Removes the top entry; its pixel and crown are left in outPix and outCrown.
 pop() {
  this.outPix = this.pix[0]; this.outCrown = this.crown[0];
  const last = --this.size;
  if (last > 0) {
   this.key[0] = this.key[last]; this.pix[0] = this.pix[last]; this.crown[0] = this.crown[last];
   let i = 0;
   for (;;) {
    const l = 2 * i + 1, r = l + 1; let m = i;
    if (l < last && this.above(l, m)) m = l;
    if (r < last && this.above(r, m)) m = r;
    if (m === i) break;
    this.swap(i, m); i = m;
   }
  }
 }
}
/**
 * Segments a canopy height grid into crowns. Returns
 * [{u, v, h, w, radii: [8], base, du, dv, pixels}], tallest first.
 */
export function segmentCrowns(grid, side, n = Math.round(Math.sqrt(grid.length))) {
 if (!grid || grid.length !== n * n) return [];
 const cell = side / n, h = smooth(grid, n);
 const label = new Int32Array(n * n).fill(-1);
 // The top to a fraction of a pixel (a parabola through the smoothed heights each way), so a
 // flat-topped crown's top is its middle, not its first pixel.
 const refine = (a, b, c) => { const d = a - 2 * b + c; return d < -1e-6 ? Math.max(-.5, Math.min(.5, .5 * (a - c) / d)) : 0; };
 const crowns = [];
 const heap = new Heap(1 << 16);
 const N8 = [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]];
 // Pass 1 takes the canopy's tall trees; passes 2 and 3 the trees under and between them that a
 // tall top's window hid (their canopy is below 45 % of it, so no tall crown took it).
 for (let pass = 0; pass < 3; pass++) {
 const seeds = tops(h, n, cell, pass ? label : null);
 if (!seeds.length) break;
 for (const i of seeds) {
  const x = i % n, y = (i / n) | 0, c = crowns.length;
  crowns.push({c, top: i, x, y, th: h[i], sum: 0, count: 0,
   fx: x + refine(h[i - 1], h[i], h[i + 1]), fy: y + refine(h[i - n], h[i], h[i + n])});
  heap.push(h[i], i, c);
 }
 while (heap.size) {
  heap.pop();
  const i = heap.outPix, c = heap.outCrown, v = h[i];
  if (label[i] >= 0) continue;
  const crown = crowns[c];
  if (crown.count > 0 && v < MEAN_SHARE * (crown.sum / crown.count)) continue;
  label[i] = c; crown.sum += v; crown.count++;
  const x = i % n, y = (i / n) | 0, maxR = maxCrownRadiusFor(crown.th) / cell;
  for (const [dx, dy] of N8) {
   const xx = x + dx, yy = y + dy;
   if (xx < 0 || yy < 0 || xx >= n || yy >= n) continue;
   const k = yy * n + xx, o = h[k];
   if (label[k] >= 0 || o < CANOPY_M || o < SEED_SHARE * crown.th) continue;
   const reach = Math.hypot(xx - crown.fx, yy - crown.fy) / maxR;
   if (reach > 1) continue;
   // downhill from the top (0.5 m of noise allowed), so a crown never climbs into its neighbour
   if (o > v + .5) continue;
   heap.push(o - .005 * reach, k, c);
  }
 }
 }
 // Shapes: the crown's reach along each of 8 directions (the farthest pixel edge of the 45
 // degree sector, measured along the sector's own direction, so a straight boundary with a
 // neighbour reads as its distance), the centroid, and the base.
 const sectors = crowns.map(() => new Float32Array(8).fill(.5 * cell));
 const DIR = Array.from({length: 8}, (_, k) => [Math.cos(k * Math.PI / 4), Math.sin(k * Math.PI / 4)]);
 const cx = new Float64Array(crowns.length), cy = new Float64Array(crowns.length), cw = new Float64Array(crowns.length);
 const open = new Int32Array(crowns.length), lows = crowns.map(() => []);
 for (let i = 0; i < n * n; i++) {
  const c = label[i]; if (c < 0) continue;
  const crown = crowns[c], x = i % n, y = (i / n) | 0;
  const ex = (x - crown.fx) * cell, ny = (crown.fy - y) * cell; // east and north of the top
  const s = ((Math.round(Math.atan2(ny, ex) / (Math.PI / 4)) % 8) + 8) % 8;
  const d = ex * DIR[s][0] + ny * DIR[s][1] + .5 * cell; // to the pixel's far edge
  if (d > sectors[c][s]) sectors[c][s] = d;
  const wgt = h[i]; cx[c] += wgt * x; cy[c] += wgt * y; cw[c] += wgt;
  // an edge pixel beside open ground shows how low the crown hangs
  let edge = false;
  for (const [dx, dy] of N8) {
   const xx = x + dx, yy = y + dy;
   if (xx < 0 || yy < 0 || xx >= n || yy >= n) continue;
   if (h[yy * n + xx] < CANOPY_M) { edge = true; break; }
  }
  if (edge) { open[c]++; lows[c].push(grid[i]); }
 }
 const out = [];
 for (const crown of crowns) {
  const c = crown.c;
  if (crown.count < 2) continue;
  // the top's height from the raw grid (the 3 x 3 maximum), not the smoothed one
  let top = 0;
  for (let j = -1; j <= 1; j++) for (let k = -1; k <= 1; k++) {
   const yy = crown.y + j, xx = crown.x + k;
   if (yy >= 0 && yy < n && xx >= 0 && xx < n) top = Math.max(top, grid[yy * n + xx]);
  }
  const area = crown.count * cell * cell, width = 2 * Math.sqrt(area / Math.PI);
  // Crown base: an open-grown crown's lowest edge pixels; in a closed stand 45 % of the height.
  let base = .45 * top;
  if (open[c] >= 3) {
   const l = lows[c].sort((a, b) => a - b);
   base = Math.min(.7 * top, Math.max(.15 * top, l[Math.floor(.1 * (l.length - 1))]));
  }
  // A crown the tile edge cuts: the cut side takes the opposite side's reach (the rest of the
  // crown is in the next tile's grid), so the crown is not drawn lopsided and moved inward.
  const radii = Array.from(sectors[c]);
  const ex0 = (crown.fx + .5) * cell, ny0 = (crown.fy + .5) * cell;
  for (let k = 0; k < 8; k++) {
   const [dx, dy] = DIR[k];
   // distance from the top to the tile edge along direction k (x east, y north)
   const toX = dx > 1e-6 ? (side - ex0) / dx : dx < -1e-6 ? ex0 / -dx : Infinity;
   const toY = dy > 1e-6 ? ny0 / dy : dy < -1e-6 ? (side - ny0) / -dy : Infinity;
   if (radii[k] + cell >= Math.min(toX, toY)) radii[k] = Math.max(radii[k], sectors[c][(k + 4) % 8]);
  }
  const r1 = v => Math.round(v * 10) / 10, r4 = v => Math.round(v * 1e4) / 1e4;
  out.push({
   u: r4((crown.fx + .5) / n), v: r4((crown.fy + .5) / n), h: top, w: r1(width),
   radii: radii.map(r1), base: r1(base),
   du: r4((cx[c] / cw[c] - crown.fx) / n), dv: r4((cy[c] / cw[c] - crown.fy) / n),
   pixels: crown.count,
  });
 }
 out.sort((a, b) => b.h - a.h || a.v - b.v || a.u - b.u);
 return out;
}
