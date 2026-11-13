// Road routing for the game's GPS (the `route` command).
//
// There is no routing server: the helper builds a small road graph from the
// same OpenStreetMap vector tiles (OpenFreeMap, zoom 14) that it already reads
// for the world and the atlas, and runs A* on it. Each leg reads only the tiles
// round its two stops plus a margin, so a short leg costs a few cached tiles.
//
// Coordinates inside the router are global zoom-14 tile units: tile x * 4096 + x
// (the vector tiles' extent), y down. The answer is lon/lat.
import {vectorTile} from './vector-tile.mjs';
import {gunzipSync} from 'node:zlib';

export const Z = 14, EXTENT = 4096, WORLD = (1 << Z) * EXTENT;
const MPH = 0.44704;
// Free-flow speed by OpenMapTiles road class (m/s), a conservative US guess.
export const SPEED = {motorway: 65 * MPH, trunk: 55 * MPH, primary: 40 * MPH, secondary: 35 * MPH, tertiary: 30 * MPH, minor: 25 * MPH, service: 12 * MPH};
const FASTEST = SPEED.motorway;
// The longest leg the router reads tiles for (tiles per side of its box).
export const MAX_TILES = 144;

export const lonLatToUnits = (lon, lat) => [
 (lon + 180) / 360 * WORLD,
 (1 - Math.asinh(Math.tan(lat * Math.PI / 180)) / Math.PI) / 2 * WORLD,
];
export const unitsToLonLat = (x, y) => [
 x / WORLD * 360 - 180,
 Math.atan(Math.sinh(Math.PI * (1 - 2 * y / WORLD))) * 180 / Math.PI,
];
/** Metres per unit at a unit row (Web Mercator scale). */
export const metresPerUnit = y => 40075016.68557849 * Math.cos(unitsToLonLat(0, y)[1] * Math.PI / 180) / WORLD;
const round6 = v => Math.round(v * 1e6) / 1e6;

/** The drivable ways of one decoded vector tile, in global units. */
export function waysOfTile(tile, tx, ty) {
 const ways = [], names = [];
 const ox = tx * EXTENT, oy = ty * EXTENT;
 const conv = (f, p) => [ox + p.x * EXTENT / f.extent, oy + p.y * EXTENT / f.extent];
 const named = tile.layers.transportation_name;
 if (named) for (let i = 0; i < named.length; i++) {
  const f = named.feature(i), p = f.properties;
  const name = p['name:en'] || p.name || p.ref;
  if (!name || f.type !== 2) continue;
  for (const line of f.loadGeometry()) names.push({name, pts: line.map(q => conv(f, q))});
 }
 const roads = tile.layers.transportation;
 if (roads) for (let i = 0; i < roads.length; i++) {
  const f = roads.feature(i), p = f.properties;
  if (f.type !== 2 || !(p.class in SPEED)) continue;
  // The game builds no tunnels (atlas.mjs leaves them out), so a route must not use one.
  if (p.brunnel === 'tunnel' || p.access === 'no') continue;
  const oneway = Number(p.oneway) || 0;
  const slow = p.class === 'service' && ['parking_aisle', 'driveway', 'drive-through'].includes(p.service) ? 3 : 1;
  for (const line of f.loadGeometry()) {
   // Keep only the part inside the tile (the buffer belongs to the neighbour).
   const pts = clipToBox(line.map(q => conv(f, q)), ox, oy, ox + EXTENT, oy + EXTENT);
   for (const run of pts) if (run.length >= 2) ways.push({cls: p.class, ramp: Number(p.ramp) === 1, oneway, slow, name: '', pts: run});
  }
 }
 nameWays(ways, names);
 return ways;
}

/** Split a polyline into the runs inside a box (Liang-Barsky per segment). */
export function clipToBox(pts, x0, y0, x1, y1) {
 const runs = [];
 let run = [];
 const push = p => { const l = run.at(-1); if (!l || l[0] !== p[0] || l[1] !== p[1]) run.push(p); };
 for (let i = 1; i < pts.length; i++) {
  const a = pts[i - 1], b = pts[i];
  let t0 = 0, t1 = 1;
  const dx = b[0] - a[0], dy = b[1] - a[1];
  let out = false;
  for (const [p, q] of [[-dx, a[0] - x0], [dx, x1 - a[0]], [-dy, a[1] - y0], [dy, y1 - a[1]]]) {
   if (p === 0) { if (q < 0) { out = true; break; } continue; }
   const t = q / p;
   if (p < 0) t0 = Math.max(t0, t); else t1 = Math.min(t1, t);
   if (t0 > t1) { out = true; break; }
  }
  if (out) { if (run.length > 1) runs.push(run); run = []; continue; }
  const pa = [a[0] + t0 * dx, a[1] + t0 * dy], pb = [a[0] + t1 * dx, a[1] + t1 * dy];
  if (run.length && (run.at(-1)[0] !== pa[0] || run.at(-1)[1] !== pa[1])) { if (run.length > 1) runs.push(run); run = []; }
  push(pa); push(pb);
  if (t1 < 1) { if (run.length > 1) runs.push(run); run = []; }
 }
 if (run.length > 1) runs.push(run);
 return runs;
}

/** Name each way from the nearest parallel named line (transportation_name carries the names). */
export function nameWays(ways, names, maxUnits = 22) {
 const cell = 64, grid = new Map();
 const key = (x, y) => Math.floor(x / cell) + ',' + Math.floor(y / cell);
 names.forEach((n, ni) => {
  for (let j = 1; j < n.pts.length; j++) {
   const a = n.pts[j - 1], b = n.pts[j];
   const seen = new Set();
   const steps = Math.max(1, Math.ceil(Math.hypot(b[0] - a[0], b[1] - a[1]) / cell));
   for (let s = 0; s <= steps; s++) {
    const k = key(a[0] + (b[0] - a[0]) * s / steps, a[1] + (b[1] - a[1]) * s / steps);
    if (seen.has(k)) continue; seen.add(k);
    if (!grid.has(k)) grid.set(k, []);
    grid.get(k).push([ni, j]);
   }
  }
 });
 for (const w of ways) {
  // Vote along the way: the name nearest to most of its segments wins.
  const votes = new Map();
  for (let j = 1; j < w.pts.length; j++) {
   const a = w.pts[j - 1], b = w.pts[j], len = Math.hypot(b[0] - a[0], b[1] - a[1]);
   if (!len) continue;
   const cx = (a[0] + b[0]) / 2, cy = (a[1] + b[1]) / 2;
   let best = maxUnits * maxUnits, name = '';
   for (let gx = -1; gx <= 1; gx++) for (let gy = -1; gy <= 1; gy++) {
    for (const [ni, k] of grid.get(key(cx + gx * cell, cy + gy * cell)) ?? []) {
     const p = names[ni].pts[k - 1], q = names[ni].pts[k];
     const dx = q[0] - p[0], dy = q[1] - p[1], ll = dx * dx + dy * dy;
     if (!ll) continue;
     if (Math.abs((dx * (b[0] - a[0]) + dy * (b[1] - a[1])) / (Math.sqrt(ll) * len)) < 0.8) continue;
     const t = Math.max(0, Math.min(1, ((cx - p[0]) * dx + (cy - p[1]) * dy) / ll));
     const d = (cx - p[0] - dx * t) ** 2 + (cy - p[1] - dy * t) ** 2;
     if (d < best) { best = d; name = names[ni].name; }
    }
   }
   if (name) votes.set(name, (votes.get(name) ?? 0) + len);
  }
  let top = 0;
  for (const [n, v] of votes) if (v > top) { top = v; w.name = n; }
 }
}

/**
 * The road graph: nodes are way vertices (merged when closer than snap units,
 * which joins the two halves of a road cut at a tile edge), edges are way
 * segments with a travel time. A way end that stops on another way's segment
 * (a T junction without a shared vertex) is joined to it.
 */
export class RoadGraph {
 constructor(snap = 2) {
  this.snap = snap;
  this.x = []; this.y = [];
  this.out = []; // per node: [{to, way, len, time}]
  this.nbrs = []; // per node: the nodes it joins (any direction)
  this.ways = [];
  this.cells = new Map();
  this.segs = new Map();
  this.ends = [];
 }
 cellKey(x, y) { return Math.floor(x / 16) + ',' + Math.floor(y / 16); }
 node(x, y) {
  const s = this.snap;
  for (let gx = -1; gx <= 1; gx++) for (let gy = -1; gy <= 1; gy++) {
   for (const n of this.cells.get(this.cellKey(x + gx * 16, y + gy * 16)) ?? []) {
    if (Math.abs(this.x[n] - x) <= s && Math.abs(this.y[n] - y) <= s) return n;
   }
  }
  const n = this.x.length;
  this.x.push(x); this.y.push(y); this.out.push([]); this.nbrs.push(new Set());
  const k = this.cellKey(x, y);
  if (!this.cells.has(k)) this.cells.set(k, []);
  this.cells.get(k).push(n);
  return n;
 }
 link(a, b, wi) {
  if (a === b) return;
  const w = this.ways[wi];
  this.nbrs[a].add(b); this.nbrs[b].add(a);
  const len = Math.hypot(this.x[b] - this.x[a], this.y[b] - this.y[a]) * metresPerUnit((this.y[a] + this.y[b]) / 2);
  const speed = w.ramp ? Math.min(SPEED[w.cls], 35 * MPH) : SPEED[w.cls];
  const time = len / speed * (w.slow ?? 1);
  if (w.oneway >= 0) this.out[a].push({to: b, way: wi, len, time});
  if (w.oneway <= 0) this.out[b].push({to: a, way: wi, len, time});
 }
 addWays(ways) {
  for (const w of ways) {
   const wi = this.ways.length;
   this.ways.push(w);
   const ids = w.pts.map(p => this.node(p[0], p[1]));
   for (let j = 1; j < ids.length; j++) if (ids[j - 1] !== ids[j]) { this.link(ids[j - 1], ids[j], wi); this.index(ids[j - 1], ids[j], wi); }
   this.ends.push([wi, ids[0]], [wi, ids.at(-1)]);
  }
 }
 /** After the last addWays: join T junctions (a lone way end on another way's segment). */
 finish() {
  for (const [wi, end] of this.ends) {
   if (this.degree(end) > 1) continue;
   const hit = this.nearestSegment(this.x[end], this.y[end], 3, wi);
   if (hit) this.splitInto(hit, end);
  }
  this.ends = [];
  return this;
 }
 /** Branches at a node: 1 a dead end, 2 a plain road point, 3 or more a junction. */
 degree(n) { return this.nbrs[n].size; }
 /** Put segment a-b in the segment grid (every cell its box touches). */
 index(a, b, way) {
  const seg = {a, b, way, live: true};
  const c = 64;
  for (let gx = Math.floor(Math.min(this.x[a], this.x[b]) / c); gx <= Math.floor(Math.max(this.x[a], this.x[b]) / c); gx++)
   for (let gy = Math.floor(Math.min(this.y[a], this.y[b]) / c); gy <= Math.floor(Math.max(this.y[a], this.y[b]) / c); gy++) {
    const k = gx + ',' + gy;
    if (!this.segs.has(k)) this.segs.set(k, []);
    this.segs.get(k).push(seg);
   }
  return seg;
 }
 /** The closest live segment to a point within maxUnits (skipping one way), with the foot point. */
 nearestSegment(x, y, maxUnits, skipWay = -1) {
  let best = null, bestD = maxUnits * maxUnits;
  const c = 64, seen = new Set();
  for (let gx = Math.floor((x - maxUnits) / c); gx <= Math.floor((x + maxUnits) / c); gx++)
   for (let gy = Math.floor((y - maxUnits) / c); gy <= Math.floor((y + maxUnits) / c); gy++)
    for (const seg of this.segs.get(gx + ',' + gy) ?? []) {
     if (!seg.live || seg.way === skipWay || seen.has(seg)) continue;
     seen.add(seg);
     const {a, b} = seg;
     const ax = this.x[a], ay = this.y[a], dx = this.x[b] - ax, dy = this.y[b] - ay, ll = dx * dx + dy * dy;
     if (!ll) continue;
     const t = Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / ll));
     const d = (x - ax - dx * t) ** 2 + (y - ay - dy * t) ** 2;
     if (d < bestD) { bestD = d; best = {seg, a, b, way: seg.way, t, x: ax + dx * t, y: ay + dy * t, d: Math.sqrt(d)}; }
    }
  return best;
 }
 /** Cut segment a-b at node n (both directions as the way allows). */
 splitInto(hit, n) {
  const {a, b, way, seg} = hit;
  if (n === a || n === b) return;
  for (const [p, q] of [[a, b], [b, a]]) this.out[p] = this.out[p].filter(e => !(e.to === q && e.way === way));
  seg.live = false;
  this.nbrs[a].delete(b); this.nbrs[b].delete(a);
  this.link(a, n, way); this.index(a, n, way);
  this.link(n, b, way); this.index(n, b, way);
 }
 /** A new node on the nearest drivable segment to a point: where a stop joins the roads. */
 attach(x, y, maxUnits) {
  const hit = this.nearestSegment(x, y, maxUnits);
  if (!hit) return null;
  if (hit.t <= 0) return {node: hit.a, offUnits: hit.d};
  if (hit.t >= 1) return {node: hit.b, offUnits: hit.d};
  const n = this.x.length;
  this.x.push(hit.x); this.y.push(hit.y); this.out.push([]); this.nbrs.push(new Set());
  this.splitInto(hit, n);
  return {node: n, offUnits: hit.d};
 }
}

/** A* by travel time; the heuristic is the straight line at motorway speed. */
export function shortestPath(graph, from, to) {
 const n = graph.x.length;
 const g = new Float64Array(n).fill(Infinity), prev = new Int32Array(n).fill(-1), prevEdge = new Array(n);
 const heap = [];
 const mpu = metresPerUnit(graph.y[to]);
 const h = v => Math.hypot(graph.x[v] - graph.x[to], graph.y[v] - graph.y[to]) * mpu / FASTEST;
 const push = (f, v) => { heap.push([f, v]); let i = heap.length - 1; while (i) { const p = (i - 1) >> 1; if (heap[p][0] <= heap[i][0]) break; [heap[p], heap[i]] = [heap[i], heap[p]]; i = p; } };
 const pop = () => { const top = heap[0], last = heap.pop(); if (heap.length) { heap[0] = last; let i = 0; for (;;) { const l = 2 * i + 1, r = l + 1; let m = i; if (l < heap.length && heap[l][0] < heap[m][0]) m = l; if (r < heap.length && heap[r][0] < heap[m][0]) m = r; if (m === i) break; [heap[m], heap[i]] = [heap[i], heap[m]]; i = m; } } return top; };
 g[from] = 0; push(h(from), from);
 const done = new Uint8Array(n);
 while (heap.length) {
  const [, v] = pop();
  if (done[v]) continue;
  done[v] = 1;
  if (v === to) break;
  for (const e of graph.out[v]) {
   const c = g[v] + e.time;
   if (c < g[e.to]) { g[e.to] = c; prev[e.to] = v; prevEdge[e.to] = e; push(c + h(e.to), e.to); }
  }
 }
 if (!Number.isFinite(g[to])) return null;
 const nodes = [to], edges = [];
 for (let v = to; v !== from; v = prev[v]) { edges.push(prevEdge[v]); nodes.push(prev[v]); }
 return {nodes: nodes.reverse(), edges: edges.reverse(), time: g[to]};
}

const bearing = (ax, ay, bx, by) => Math.atan2(bx - ax, -(by - ay)) * 180 / Math.PI; // compass, y down
const wrap = d => ((d + 540) % 360) - 180;

/** The turn a driver makes for a heading change (positive = right). */
export function turnKind(delta) {
 const a = Math.abs(delta), side = delta > 0 ? 'right' : 'left';
 if (a < 25) return 'straight';
 if (a < 50) return 'slight ' + side;
 if (a < 135) return side;
 if (a < 165) return 'sharp ' + side;
 return 'u-turn';
}

/**
 * Turn-by-turn steps along a path: a step where the street name changes or
 * where the road bends at a junction (more than two ways meet) by 25 degrees
 * or more. Headings are measured over ~20 m each side, so a filleted corner
 * still reads as one turn.
 */
export function stepsOfPath(graph, path) {
 const {nodes, edges} = path;
 const cum = [0];
 for (const e of edges) cum.push(cum.at(-1) + e.len);
 const nameAt = i => graph.ways[edges[i].way].name;
 const headingAround = (i, dir) => {
  // From node i, ~20 m back (dir -1) or ahead (dir +1).
  let j = i;
  while (j + dir >= 0 && j + dir < nodes.length && Math.abs(cum[j + dir] - cum[i]) < 20) j += dir;
  if (j === i) j = Math.max(0, Math.min(nodes.length - 1, i + dir));
  const [a, b] = dir < 0 ? [j, i] : [i, j];
  return bearing(graph.x[nodes[a]], graph.y[nodes[a]], graph.x[nodes[b]], graph.y[nodes[b]]);
 };
 const steps = [{index: 0, at_m: 0, kind: 'depart', angle: 0, name: edges.length ? nameAt(0) : ''}];
 for (let i = 1; i < nodes.length - 1; i++) {
  const delta = wrap(headingAround(i, 1) - headingAround(i, -1));
  const renamed = nameAt(i) !== nameAt(i - 1) && nameAt(i) !== '';
  const junction = graph.degree(nodes[i]) > 2;
  const kind = turnKind(delta);
  if ((junction && kind !== 'straight') || (renamed && junction)) {
   // Two steps closer than 15 m are one manoeuvre: keep the sharper.
   const last = steps.at(-1);
   if (last.kind !== 'depart' && cum[i] - last.at_m < 15) {
    if (Math.abs(delta) > Math.abs(last.angle)) Object.assign(last, {index: i, at_m: cum[i], kind, angle: delta});
    last.name = nameAt(i) || last.name;
    continue;
   }
   steps.push({index: i, at_m: cum[i], kind: kind === 'straight' ? 'continue' : kind, angle: delta, name: nameAt(i)});
  }
 }
 steps.push({index: nodes.length - 1, at_m: cum.at(-1), kind: 'arrive', angle: 0, name: ''});
 return steps.map(s => ({...s, angle: Math.round(s.angle), at_m: Math.round(s.at_m * 10) / 10}));
}

/** Tiles (zoom 14) that cover two points plus margin tiles round them. */
export function tilesFor(a, b, margin) {
 const t = p => [Math.floor(p[0] / EXTENT), Math.floor(p[1] / EXTENT)];
 const [ax, ay] = t(a), [bx, by] = t(b);
 const out = [];
 for (let x = Math.min(ax, bx) - margin; x <= Math.max(ax, bx) + margin; x++)
  for (let y = Math.min(ay, by) - margin; y <= Math.max(ay, by) + margin; y++) out.push([x, y]);
 return out;
}

/**
 * Route through stops [[lon, lat], ...]. loadWays(tx, ty) returns the
 * drivable ways of one tile (waysOfTile of the decoded tile, cached).
 */
export async function route(stops, loadWays, {maxAttachM = 250, margins} = {}) {
 if (stops.length < 2) throw Error('A route needs two stops or more');
 const legs = [];
 for (let s = 1; s < stops.length; s++) {
  const a = lonLatToUnits(...stops[s - 1]), b = lonLatToUnits(...stops[s]);
  let leg = null, why = 'no road joins the two stops';
  // A short leg reads a tile all round; a long one first only its own box (the road
  // between two far stops seldom leaves it), and wider only when that finds nothing.
  for (const margin of margins ?? (tilesFor(a, b, 1).length <= 36 ? [1, 2] : [0, 1])) {
   const tiles = tilesFor(a, b, margin);
   if (tiles.length > MAX_TILES) { why = 'the stops are too far apart: add a stop between them'; break; }
   const graph = new RoadGraph();
   for (const [tx, ty] of tiles) graph.addWays(await loadWays(tx, ty));
   graph.finish();
   const mpu = metresPerUnit(a[1]);
   const from = graph.attach(a[0], a[1], maxAttachM / mpu), to = graph.attach(b[0], b[1], maxAttachM / mpu);
   if (!from || !to) { why = `stop ${from ? s + 1 : s} is not near a road`; break; }
   const path = from.node === to.node ? {nodes: [from.node], edges: [], time: 0} : shortestPath(graph, from.node, to.node);
   if (!path) continue;
   leg = {
    length_m: Math.round(path.edges.reduce((t, e) => t + e.len, 0) * 10) / 10,
    duration_s: Math.round(path.time),
    points: path.nodes.map(n => unitsToLonLat(graph.x[n], graph.y[n]).map(round6)),
    steps: stepsOfPath(graph, path),
    names: path.edges.map(e => graph.ways[e.way].name),
   };
   // One name per point after the first, run-length coded for the game: [[first index, name], ...].
   const runs = [];
   leg.names.forEach((n, i) => { if (!runs.length || runs.at(-1)[1] !== n) runs.push([i, n]); });
   leg.names = runs;
   break;
  }
  if (!leg) return {status: 'no-route', leg: s, reason: why};
  legs.push(leg);
 }
 return {
  status: 'ok',
  length_m: Math.round(legs.reduce((t, l) => t + l.length_m, 0) * 10) / 10,
  duration_s: legs.reduce((t, l) => t + l.duration_s, 0),
  legs,
  attribution: '© OpenStreetMap contributors (ODbL), tiles by OpenFreeMap',
 };
}

/** The helper's tile reader for the router: the cached zoom-14 vector tiles of OpenFreeMap. */
export function tileWaysReader(data, day = 86400000) {
 const memo = new Map();
 let meta = null;
 return async (tx, ty) => {
  const key = tx + ',' + ty;
  if (memo.has(key)) return memo.get(key);
  meta ??= JSON.parse((await data('manifest', 'https://tiles.openfreemap.org/planet', day)).toString());
  const url = meta.tiles[0].replace('{z}', String(Z)).replace('{x}', tx).replace('{y}', ty);
  let ways = [];
  try {
   const raw = await data('v-' + tx + '-' + ty, url);
   ways = waysOfTile(vectorTile(raw[0] === 31 ? gunzipSync(raw) : raw), tx, ty);
  } catch { ways = []; }
  memo.set(key, ways);
  return ways;
 };
}

/** "lon,lat" arguments to stops. */
export function parseStops(args) {
 const stops = args.map(a => a.split(',').map(Number)).filter(p => p.length === 2 && p.every(Number.isFinite));
 if (stops.length !== args.length) throw Error('Stops must be lon,lat pairs');
 if (stops.length > 12) throw Error('A route can have 12 stops or fewer');
 for (const [lon, lat] of stops) if (Math.abs(lat) > 82 || Math.abs(lon) > 180) throw Error('A stop is outside the map');
 return stops;
}
