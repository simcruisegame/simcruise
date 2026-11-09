import fs from 'node:fs/promises';
import path from 'node:path';
import {gunzipSync} from 'node:zlib';
import {vectorTile} from '../../helper/vector-tile.mjs';

const [directory, key, latitude, longitude] = process.argv.slice(2);
const lat = Number(latitude), lon = Number(longitude);
if (!directory || !/^[a-z0-9-]+$/.test(key || '') || !Number.isFinite(lat) || !Number.isFinite(lon)) {
  throw Error('Use bun vector.mjs WORK KEY LATITUDE LONGITUDE');
}
await fs.mkdir(directory, {recursive: true});
const target = path.join(directory, key + '-map.json');
const x = Math.floor((lon + 180) / 360 * 16384);
const y = Math.floor((1 - Math.asinh(Math.tan(lat * Math.PI / 180)) / Math.PI) * 8192);

async function limited(url, limit) {
  const response = await fetch(url, {signal: AbortSignal.timeout(18000)});
  if (!response.ok) throw Error('OpenFreeMap returned ' + response.status);
  const chunks = [];
  let size = 0;
  for await (const chunk of response.body) {
    size += chunk.byteLength;
    if (size > limit) throw Error('OpenFreeMap size limit');
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}
async function readTile() {
  const catalog = JSON.parse(await limited('https://tiles.openfreemap.org/planet', 72000));
  const url = new URL(catalog.tiles[0].replace('{z}', '14').replace('{x}', x).replace('{y}', y));
  if (url.origin !== 'https://tiles.openfreemap.org') throw Error('Unexpected map tile origin');
  const raw = await limited(url, 700000);
  const tile = vectorTile(raw[0] === 31 ? gunzipSync(raw) : raw);
  const result = {url: url.href};
  for (const [kind, source] of [['road','transportation'], ['name','transportation_name']]) {
    const layer = tile.layers[source], lines = [];
    if (!layer) throw Error('Missing road layer ' + source);
    for (let i = 0; i < layer.length; i++) {
      const feature = layer.feature(i);
      if (feature.type === 2 && feature.properties.brunnel !== 'tunnel') {
        lines.push(feature.toGeoJSON(x, y, 14));
      }
    }
    result[kind] = lines;
  }
  await fs.writeFile(target, JSON.stringify(result), {flag: 'wx'});
}
try { await fs.access(target); } catch { await readTile(); }
