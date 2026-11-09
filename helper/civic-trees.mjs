import fs from 'node:fs';
import path from 'node:path';
import {cacheReader} from './atlas-cache.mjs';

const day = 86400000;
const world = 40075016.68557849;
const tileWidth = world / 65536;
const rowLimit = 650;
const byteLimit = 448 * 1024;
const directory = [
  {
    id: 'lincoln-ne',
    label: 'City of Lincoln / Lancaster County Geospatial Services, Lincoln Parks and Recreation',
    extent: [-10784664.50,4967915.72,-10750054.88,4996545.03],
    url: 'https://gis.lincoln.ne.gov/public/rest/services/Parks/Trees/MapServer/0/query',
    where: "LifeCycle='Active' AND DBH >= 1 AND (MultiTrunk=0 OR MultiTrunk IS NULL) AND SiteLocation IN ('Street','Park','Median','Trail','Other - Public','Boundary','Golf Course')",
    diameter: 'DBH',
  },
  {
    id: 'cambridge-ma',
    label: 'City of Cambridge GIS and Cambridge Public Works',
    extent: [-7921459.72,5214062.13,-7911334.97,5221629.39],
    url: 'https://services1.arcgis.com/WnzC35krSYGuYov4/arcgis/rest/services/Trees/FeatureServer/0/query',
    where: "SiteType='Tree' AND diameter>=1 AND (trunks<=1 OR trunks IS NULL) AND RemovalDate IS NULL AND (ScheduledRemoval=0 OR ScheduledRemoval IS NULL)",
    diameter: 'diameter',
    planted: ['PlantDate','CartegraphPlantDate'],
  },
];

const conifer = /^(Abies|Cedrus|Juniperus|Larix|Picea|Pinus|Pseudotsuga|Taxodium|Taxus|Thuja|Tsuga)\b/i;
const round = number => Math.round(number * 1e6) / 1e6;
const empty = () => ({source: null, stems: []});

function normalize(payload, west, north, source) {
  if (!Array.isArray(payload.features) || payload.exceededTransferLimit || payload.features.length > rowLimit)
    throw Error('Incomplete city inventory');
  const stems = [];
  for (const {geometry, attributes} of payload.features) {
    if (!geometry || !attributes) continue;
    const {TreeID: id} = attributes;
    if (typeof id !== 'string' || !/^[\w-]{1,32}$/.test(id)) continue;
    const u = (geometry.x - west) / tileWidth;
    const v = (north - geometry.y) / tileWidth;
    const dbh = attributes[source.diameter] * .0254;
    if (!(u >= 0 && u < 1 && v >= 0 && v < 1 && dbh >= .025 && dbh <= 1.8)) continue;
    const species = String(attributes.ScientificName ?? '').slice(0,96);
    const name = String(attributes.CommonName ?? '').slice(0,96);
    // Retain observed taxon even though the renderer uses representative tree meshes.
    const plantedAt = Math.max(0,...(source.planted ?? []).map(field => Number(attributes[field]) || 0));
    stems.push({...(plantedAt > 0 ? {plantedAt} : {}),provider:source.id, id, u:round(u), v:round(v), dbh:round(dbh), species, name, kind:conifer.test(species) ? 2 : 0});
  }
  return stems;
}

async function boundedJson(response) {
  if (!response.body || Number(response.headers.get('Content-Length')) > byteLimit) {
    await response.body?.cancel();
    throw Error('Tree inventory above limit');
  }
  const reader = response.body.getReader();
  const parts = [];
  let total = 0;
  for (;;) {
    const {done, value} = await reader.read();
    if (done) break;
    total += value.length;
    if (total > byteLimit) {
      await reader.cancel();
      throw Error('Tree inventory above limit');
    }
    parts.push(value);
  }
  return JSON.parse(Buffer.concat(parts));
}

export async function cityInventory(x, y, cache, onRequest, fetcher = fetch) {
  if (![x,y].every(n => Number.isInteger(n) && n >= 0 && n < 65536)) return empty();
  const west = x * tileWidth - world/2;
  const north = world/2 - y * tileWidth;
  const bounds = [west, north - tileWidth, west + tileWidth, north];
  const source = directory.find(({extent}) => west < extent[2] && bounds[2] > extent[0] && bounds[1] < extent[3] && north > extent[1]);
  if (!source) return empty();
  const key = (source.planted ? 'inventory-v3-' : 'inventory-v2-') + source.id + '-' + x + '-' + y + '.json';
  const miss = path.join(cache, key + '.miss');
  if (Date.now() - (fs.statSync(miss,{throwIfNoEntry:false})?.mtimeMs ?? 0) < 3600000) return empty();

  const url = new URL(source.url);
  url.search = new URLSearchParams({
    f:'json', geometry:bounds.join(','), geometryType:'esriGeometryEnvelope', inSR:'3857', outSR:'3857',
    where:source.where, outFields:['TreeID','ScientificName','CommonName',source.diameter,...(source.planted ?? [])].join(','),
    returnGeometry:'true', returnZ:'false', returnM:'false', resultRecordCount:String(rowLimit),
  }).toString();
  const validate = async (address, options) => {
    const response = await fetcher(address,{...options, redirect:'error'});
    if (!response.ok) return response;
    const stems = normalize(await boundedJson(response), west, north, source);
    return new Response(JSON.stringify(stems));
  };
  try {
    const read = cacheReader(cache, onRequest, validate);
    const value = await read(key, url.href, 30*day, null, 4000);
    return {source:source.label, stems:JSON.parse(value)};
  } catch {
    fs.writeFileSync(miss,'');
    return empty();
  }
}

function twoNearest(subject, rows, distance) {
  let index = -1, first = Infinity, second = Infinity;
  for (let i = 0; i < rows.length; i++) {
    const d = distance(subject, rows[i]);
    if (d < first) {
      second = first;
      first = d;
      index = i;
    } else if (d < second) second = d;
  }
  return {index,first,second};
}

function median(values) {
  values.sort((a,b) => a-b);
  return values[Math.floor(values.length/2)];
}

function continuousCanopy(grid, stem, crown, distance) {
  if (!grid || grid.length !== 512 * 512 || stem.dbh < .20 || crown[2] < 6) return false;
  if (distance > Math.min(9.5, crown[3] * .81)) return false;
  // Test the actual height raster between a measured trunk and an off-centre apex.
  let total = 0;
  for (let step = 0; step <= 8; step++) {
    const t = step / 8;
    const x = Math.floor(512 * (stem.u + (crown[0] - stem.u) * t));
    const y = Math.floor(512 * (stem.v + (crown[1] - stem.v) * t));
    const height = grid[y * 512 + x];
    if (!(height >= crown[2] * .65)) return false;
    total += height;
  }
  return total / 9 >= crown[2] * .80;
}

// Extended rows keep the original peak coordinates for the renderer's wall and pavement checks.
export function anchorCrowns(crowns, stems, side, grid = null) {
  if (!crowns.length || !stems.length) return {trees:crowns, matched:0, estimated:0};
  const trees = crowns.slice();
  const points = stems.map(stem => [stem.u, stem.v]);
  const distance = (a,b) => side * Math.hypot(a[0]-b[0],a[1]-b[1]);
  const nearestPeaks = points.map(p => twoNearest(p, crowns, distance));
  const nearestStems = crowns.map(c => twoNearest(c, points, distance));
  const peers = [], remaining = [];
  let matched = 0, estimated = 0;
  for (let i = 0; i < stems.length; i++) {
    const stem = stems[i], point = points[i];
    // Neighbor tiles and closely duplicated records don't establish unique ownership.
    if (Math.min(stem.u,stem.v,1-stem.u,1-stem.v)*side < 8 || points.some((p,k) => k !== i && distance(point,p) < 2)) continue;
    const peak = nearestPeaks[i], reverse = nearestStems[peak.index], crown = crowns[peak.index];
    const radius = Math.min(7,Math.max(2.6,crown[3]*.64));
    const margin = Math.min(peak.second-peak.first, reverse.second-peak.first);
    const reciprocal = reverse.index === i && margin > 1.3;
    const centered = peak.first <= radius;
    const continuous = reciprocal && !centered && margin >= 2.5 && continuousCanopy(grid,stem,crown,peak.first);
    if (reciprocal && (centered || continuous) && (stem.dbh >= .1 || crown[2] <= 8)) {
      trees[peak.index] = [stem.u,stem.v,crown[2],crown[3],stem.dbh,stem.name,stem.kind,crown[0],crown[1],stem.id,'matched',stem.provider];
      peers.push({dbh:stem.dbh, kind:stem.kind, h:crown[2], w:crown[3]});
      matched++;
    } else if (peak.first > Math.max(8,crown[3]*.75) && stem.dbh >= .14) {
      remaining.push({stem,point});
    }
  }
  for (const {stem,point} of remaining) {
    const eligible = peers.filter(peer => peer.kind === stem.kind && Math.abs(Math.log(peer.dbh/stem.dbh)) <= .36);
    if (eligible.length < 5) continue;
    const h = median(eligible.map(peer => peer.h));
    const w = median(eligible.map(peer => peer.w));
    if (trees.some(crown => distance(point,crown) < (crown.length > 4 ? 3.5 : Math.max(4,.41*(w+crown[3]))))) continue;
    trees.push([stem.u,stem.v,h,w,stem.dbh,stem.name,stem.kind,stem.u,stem.v,stem.id,'peer estimate',stem.provider]);
    estimated++;
  }
  return {trees,matched,estimated};
}
