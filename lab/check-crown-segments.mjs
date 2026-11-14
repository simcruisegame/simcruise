// Offline checks of helper/crown-segments.mjs on synthetic canopy height grids (1 m cells).
import assert from 'node:assert/strict';
import {segmentCrowns, crownWidthFor} from '../helper/crown-segments.mjs';

const N = 512, SIDE = 512;
// A paraboloid crown: height H at (cx, cy) metres (x east, y south), radii rx, ry.
function grid(trees, floor = 0) {
 const g = new Uint8Array(N * N).fill(floor);
 for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
  let v = g[y * N + x];
  for (const t of trees) {
   const ex = (x + .5 - t.cx) / t.rx, ey = (y + .5 - t.cy) / (t.ry ?? t.rx), r2 = ex * ex + ey * ey;
   if (r2 < 1) v = Math.max(v, Math.round(t.h * (1 - r2)));
  }
  g[y * N + x] = v;
 }
 return g;
}
const near = (a, b, tol, what) => assert.ok(Math.abs(a - b) <= tol, `${what}: ${a} vs ${b} (tolerance ${tol})`);

// One open-grown tree: one crown, its top, height and a round shape.
{
 const c = segmentCrowns(grid([{cx: 256, cy: 256, h: 20, rx: 6}]), SIDE);
 assert.equal(c.length, 1, 'one tree');
 near(c[0].u * SIDE, 256, 1, 'top x'); near(c[0].v * SIDE, 256, 1, 'top y');
 assert.equal(c[0].h, 20);
 // the crown reaches down to 45 % of the top: r = 6 sqrt(0.55) = 4.4 m
 near(c[0].w, 2 * 6 * Math.sqrt(.55), 1.5, 'width');
 for (const r of c[0].radii) near(r, 6 * Math.sqrt(.55), 1.3, 'radius');
 // open ground round it: the base comes from the lowest edge pixels, within 15-70 % of h
 assert.ok(c[0].base >= 3 && c[0].base <= 14, `base ${c[0].base}`);
 near(c[0].du * SIDE, 0, .6, 'no lean'); near(c[0].dv * SIDE, 0, .6, 'no lean');
}

// Two overlapping crowns 11 m apart: two tops at the right places, the taller first.
{
 const c = segmentCrowns(grid([{cx: 200, cy: 300, h: 30, rx: 12}, {cx: 211, cy: 300, h: 24, rx: 10}]), SIDE);
 assert.equal(c.length, 2, 'two trees');
 assert.equal(c[0].h, 30); assert.equal(c[1].h, 24);
 near(c[0].u * SIDE, 200, .7, 'first x'); near(c[1].u * SIDE, 211, .7, 'second x');
 // the crowns meet in the valley between them (6.9 m east of the first top), short of the
 // first crown's open side (8.9 m to the west, where it reaches 45 % of its height)
 near(c[0].radii[0], 6.9, 1.2, 'first tree, east'); near(c[0].radii[4], 8.9, 1.2, 'first tree, west');
 assert.ok(c[0].radii[0] + 1 < c[0].radii[4], `first tree: east ${c[0].radii[0]} < west ${c[0].radii[4]}`);
 assert.ok(c[1].radii[4] + 1 < c[1].radii[0], `second tree: west ${c[1].radii[4]} < east ${c[1].radii[0]}`);
}

// An elliptical crown, wide east-west: the east and west radii about twice north and south.
{
 const [c] = segmentCrowns(grid([{cx: 300, cy: 200, h: 25, rx: 9, ry: 4.5}]), SIDE);
 const ew = (c.radii[0] + c.radii[4]) / 2, ns = (c.radii[2] + c.radii[6]) / 2;
 assert.ok(ew / ns > 1.6 && ew / ns < 2.4, `east-west ${ew} / north-south ${ns}`);
}

// A tall tree beside a small one outside its window: both found; the small one keeps its own crown.
{
 const c = segmentCrowns(grid([{cx: 100, cy: 100, h: 30, rx: 7}, {cx: 112, cy: 100, h: 8, rx: 3}]), SIDE);
 assert.equal(c.length, 2, `tall and small: ${JSON.stringify(c.map(t => [t.h, t.u * SIDE]))}`);
 assert.equal(c[1].h, 8);
}

// A crown the tile's west edge cuts: its west reach is taken from its east reach.
{
 const [c] = segmentCrowns(grid([{cx: 5, cy: 256, h: 26, rx: 11}]), SIDE);
 near(c.u * SIDE, 5, .7, 'edge tree x');
 assert.equal(c.radii[4], c.radii[0], `west ${c.radii[4]} = east ${c.radii[0]}`);
 assert.ok(c.radii[0] > 7, `east reach ${c.radii[0]}`);
}
// A crown whose top is across the edge (in the next tile) has no top here: no half tree.
assert.equal(segmentCrowns(grid([{cx: -3, cy: 256, h: 26, rx: 11}]), SIDE).length, 0, 'top in the next tile');

// Shrubs and grass under 3 m: no trees.
assert.equal(segmentCrowns(grid([{cx: 256, cy: 256, h: 2, rx: 5}], 1), SIDE).length, 0, 'shrubs');
assert.equal(segmentCrowns(new Uint8Array(N * N), SIDE).length, 0, 'bare ground');

// A closed stand (canopy at 12 m everywhere, bumps for crowns): the base is 45 % of the height.
{
 const trees = [];
 for (let y = 40; y < 480; y += 9) for (let x = 40; x < 480; x += 9) trees.push({cx: x + (y % 2) * 4, cy: y, h: 18, rx: 5.5});
 const c = segmentCrowns(grid(trees, 12), SIDE);
 assert.ok(c.length > .8 * trees.length && c.length < 1.2 * trees.length, `stand: ${c.length} crowns for ${trees.length} trees`);
 const inner = c.filter(t => t.u > .2 && t.u < .8 && t.v > .2 && t.v < .8);
 assert.ok(inner.every(t => t.base === Math.round(.45 * t.h * 10) / 10), 'closed-stand base');
 // each crown stays near the window rule's width for its height (no crown swallows a neighbour)
 assert.ok(inner.every(t => t.w < 1.6 * crownWidthFor(t.h) + 4), 'no merged crowns');
}

// A crown leaning south-east of its top: the centre offset points that way.
{
 const g = grid([{cx: 256, cy: 256, h: 20, rx: 6}]);
 for (let y = 256; y < 266; y++) for (let x = 256; x < 266; x++) {
  const d = Math.hypot(x - 256, y - 256);
  if (d < 9) g[y * N + x] = Math.max(g[y * N + x], Math.round(19 - 0.6 * d));
 }
 const [c] = segmentCrowns(g, SIDE);
 assert.ok(c.du > 0 && c.dv > 0, `lean ${c.du}, ${c.dv}`);
}

// Deterministic, and fast on a dense random forest.
{
 let s = 12345;
 const rnd = () => ((s = (s * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
 const trees = Array.from({length: 2500}, () => ({cx: 8 + rnd() * 496, cy: 8 + rnd() * 496, h: 6 + rnd() * 30, rx: 2.5 + rnd() * 6}));
 const g = grid(trees);
 const t0 = performance.now();
 const a = segmentCrowns(g, SIDE);
 const ms = performance.now() - t0;
 assert.deepEqual(segmentCrowns(g, SIDE), a, 'deterministic');
 assert.ok(a.length > 500, `random forest: ${a.length} crowns`);
 assert.ok(ms < 1500, `segmentation took ${ms.toFixed(0)} ms`);
 console.log(`random forest: ${a.length} crowns from ${trees.length} trees in ${ms.toFixed(0)} ms`);
}
console.log('crown segments: all checks passed');
