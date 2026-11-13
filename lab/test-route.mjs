// The GPS router (helper/route.mjs) on synthetic road maps: no network.
//   bun test ./lab/test-route.mjs
import assert from 'node:assert/strict';
import {test} from 'node:test';
import {
 RoadGraph, route, shortestPath, stepsOfPath, clipToBox, nameWays, turnKind, parseStops, tilesFor,
 lonLatToUnits, unitsToLonLat, metresPerUnit, EXTENT,
} from '../helper/route.mjs';

// Miami Beach: Ocean Drive at 5th Street.
const O = lonLatToUnits(-80.1312, 25.7781);
const mpu = metresPerUnit(O[1]);
/** A point x metres east and y metres south of the origin, in units. */
const at = (x, y) => [O[0] + x / mpu, O[1] + y / mpu];
const ll = (x, y) => unitsToLonLat(...at(x, y));
const way = (cls, name, pts, oneway = 0) => ({cls, ramp: false, oneway, slow: 1, name, pts: pts.map(p => at(...p))});
const tileOf = p => [Math.floor(p[0] / EXTENT), Math.floor(p[1] / EXTENT)];
/** All ways go to the tile that holds their first point; other tiles are empty. */
const loader = ways => async (tx, ty) => ways.filter(w => { const [x, y] = tileOf(w.pts[0]); return x === tx && y === ty; });

test('units and lon/lat round trip', () => {
 const [lon, lat] = unitsToLonLat(...lonLatToUnits(-80.1312, 25.7781));
 assert.ok(Math.abs(lon + 80.1312) < 1e-9 && Math.abs(lat - 25.7781) < 1e-9);
 // A zoom-14 tile at 25.8 degrees is about 2.2 km wide.
 assert.ok(Math.abs(mpu * EXTENT - 2204) < 20, String(mpu * EXTENT));
});

test('a straight road: length, time and the depart and arrive steps', async () => {
 const r = await route([ll(0, 0), ll(500, 0)], loader([way('minor', 'Ocean Drive', [[-100, 0], [600, 0]])]));
 assert.equal(r.status, 'ok');
 assert.ok(Math.abs(r.length_m - 500) < 2, String(r.length_m));
 assert.ok(Math.abs(r.duration_s - 500 / (25 * 0.44704)) < 2);
 assert.deepEqual(r.legs[0].steps.map(s => s.kind), ['depart', 'arrive']);
 assert.equal(r.legs[0].steps[0].name, 'Ocean Drive');
 assert.deepEqual(r.legs[0].names, [[0, 'Ocean Drive']]);
});

test('a right turn onto a named street at a junction', async () => {
 const ways = [
  way('minor', 'Ocean Drive', [[-200, 0], [0, 0], [200, 0]]),
  way('minor', '5th Street', [[0, -200], [0, 0], [0, 300]]),
 ];
 // East along Ocean Drive, then south: a right turn (y is south).
 const r = await route([ll(-150, 0), ll(0, 250)], loader(ways));
 assert.equal(r.status, 'ok');
 const steps = r.legs[0].steps;
 assert.deepEqual(steps.map(s => s.kind), ['depart', 'right', 'arrive']);
 assert.equal(steps[1].name, '5th Street');
 assert.ok(Math.abs(steps[1].at_m - 150) < 2);
 assert.ok(Math.abs(steps[1].angle - 90) <= 2);
 // And the other way round: north then west is a left turn.
 const back = await route([ll(0, 250), ll(-150, 0)], loader(ways));
 assert.equal(back.legs[0].steps[1].kind, 'left');
 assert.equal(back.legs[0].steps[1].name, 'Ocean Drive');
});

test('one-way streets are driven only their way', async () => {
 // The short road is one-way west; going east must take the loop to the south.
 const ways = [
  way('minor', 'Short', [[0, 0], [400, 0]], -1),
  way('minor', 'Loop', [[0, 0], [0, 200], [400, 200], [400, 0]]),
 ];
 const east = await route([ll(1, 0), ll(399, 0)], loader(ways));
 assert.ok(east.length_m > 700, String(east.length_m));
 const west = await route([ll(399, 0), ll(1, 0)], loader(ways));
 assert.ok(west.length_m < 410, String(west.length_m));
});

test('road class speed: a longer main road beats a slow service road', async () => {
 const ways = [
  way('service', 'Alley', [[0, 0], [1000, 0]]),
  way('primary', 'Collins Avenue', [[0, 0], [0, 150], [1000, 150], [1000, 0]]),
 ];
 const r = await route([ll(0, 0), ll(1000, 0)], loader(ways));
 assert.ok(r.length_m > 1200, 'took the main road: ' + r.length_m);
});

test('a road cut at a tile edge joins again', () => {
 const g = new RoadGraph();
 const a = way('minor', 'A', [[0, 0], [100, 0]]);
 const b = way('minor', 'A', [[100.5, 0], [200, 0]]); // half a metre short: one unit
 g.addWays([a]); g.addWays([b]); g.finish();
 const from = g.attach(...at(0, 0), 5), to = g.attach(...at(200, 0), 5);
 assert.ok(shortestPath(g, from.node, to.node));
});

test('a T junction without a shared vertex is joined', () => {
 const g = new RoadGraph();
 g.addWays([way('minor', 'Main', [[0, 0], [400, 0]]), way('minor', 'Side', [[200, 1], [200, 300]])]);
 g.finish();
 const from = g.attach(...at(0, 0), 5), to = g.attach(...at(200, 300), 5);
 const path = shortestPath(g, from.node, to.node);
 assert.ok(path, 'joined');
 const steps = stepsOfPath(g, path);
 assert.equal(steps[1].kind, 'right');
});

test('no route between roads that never meet', async () => {
 const ways = [way('minor', 'A', [[0, 0], [100, 0]]), way('minor', 'B', [[0, 500], [100, 500]])];
 const r = await route([ll(0, 0), ll(100, 500)], loader(ways), {margins: [0]});
 assert.equal(r.status, 'no-route');
 assert.equal(r.leg, 1);
});

test('a stop far from every road is refused', async () => {
 const r = await route([ll(0, 0), ll(1000, 1000)], loader([way('minor', 'A', [[0, 0], [100, 0]])]), {margins: [0]});
 assert.equal(r.status, 'no-route');
 assert.match(r.reason, /stop 2 is not near a road/);
});

test('several stops make several legs', async () => {
 const ways = [way('minor', 'Ocean Drive', [[0, 0], [1000, 0]])];
 const r = await route([ll(0, 0), ll(400, 0), ll(900, 0)], loader(ways));
 assert.equal(r.legs.length, 2);
 assert.ok(Math.abs(r.legs[0].length_m - 400) < 2 && Math.abs(r.legs[1].length_m - 500) < 2);
 assert.ok(Math.abs(r.length_m - 900) < 3);
});

test('turn kinds', () => {
 assert.equal(turnKind(10), 'straight');
 assert.equal(turnKind(-35), 'slight left');
 assert.equal(turnKind(90), 'right');
 assert.equal(turnKind(150), 'sharp right');
 assert.equal(turnKind(-178), 'u-turn');
});

test('clip to a box keeps only the inside runs', () => {
 const runs = clipToBox([[-10, 5], [5, 5], [5, 20], [15, 20], [15, 5]], 0, 0, 10, 10);
 assert.deepEqual(runs, [[[0, 5], [5, 5], [5, 10]]]);
});

test('names come from the nearest parallel named line', () => {
 const ways = [way('minor', '', [[0, 0], [100, 0]]), way('minor', '', [[0, 0], [0, 100]])];
 nameWays(ways, [{name: 'Ocean Drive', pts: [at(-10, 2), at(120, 2)]}, {name: '5th Street', pts: [at(1, -10), at(1, 110)]}]);
 assert.equal(ways[0].name, 'Ocean Drive');
 assert.equal(ways[1].name, '5th Street');
});

test('stops are parsed and checked', () => {
 assert.deepEqual(parseStops(['-80.1,25.7', '-80.2,25.8']), [[-80.1, 25.7], [-80.2, 25.8]]);
 assert.throws(() => parseStops(['x,1']));
 assert.throws(() => parseStops(['0,85']));
});

test('tiles for a leg: the box of both stops plus the margin', () => {
 const a = [5 * EXTENT + 10, 7 * EXTENT + 10], b = [6 * EXTENT + 10, 7 * EXTENT + 10];
 assert.equal(tilesFor(a, b, 0).length, 2);
 assert.equal(tilesFor(a, b, 1).length, 4 * 3);
});
