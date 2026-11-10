import fs from 'node:fs';
import path from 'node:path';

const managed = /^(?:manifest|[vh]-\d+-\d+|a-(?:hi-naip-|ak-usfs-)?\d+-\d+\.(?:jpg|png)(?:\.miss)?|s-[0-9a-f]+|(?:usgs-dem|roof-wesm|roof-ept|roof-laz|roof-district-v[12]|road-crossfall-v1|lane-layout-v1|lane-widths-v1|lane-counts-v1|lane-roads|snow-terra|ridge-aerial|summit-snow|snow-stac|ridge-band|shrub|banks-v1|forest-v1|relief|naip-low|ov|nsi-uses|vmap|scene-list|street-osm|panoramax|inventory|crown-poly|civic-pits|civic-pavement|metar|kv\d+|[cs]\d+)-[A-Za-z0-9._-]+)$/;
const handoffMs = 60_000;
// Per-tile retry records (usgs-ground.mjs elevationGate) only matter inside
// their 20-minute retry window; older ones are removed whatever the budget.
const staleState = /^elevation-retry-\d+-\d+\.json$/;
const staleStateMs = 86_400_000;

function trimSourceState(absolute, time) {
 const state = path.join(absolute, '.source-state');
 let names = [];
 try { names = fs.readdirSync(state); } catch { return; }
 for (const name of names) {
  if (!staleState.test(name)) continue;
  const file = path.join(state, name);
  const st = fs.lstatSync(file, {throwIfNoEntry:false});
  if (st?.isFile() && time - st.mtimeMs > staleStateMs) fs.rmSync(file, {force:true});
 }
}

// The helper gives Godot image filenames and exits before Godot opens them.
// Preserve access recency independently of the modification date used by HTTP TTLs.
export function trimSourceCache(directory, budget, result, time = Date.now()) {
 const absolute = path.resolve(directory);
 trimSourceState(absolute, time);
 const protectedFiles = new Set();
 const files = [result?.aerial, result?.grid, result?.middle, result?.roadMask, result?.seaMask, result?.manifest, result?.png, result?.file, ...(Array.isArray(result?.images) ? result.images : []).map(image => image?.name), ...(Array.isArray(result?.photos) ? result.photos : []).map(p => p?.name), ...(result?.frames ?? []).map(f => f.imagePath)];
 for (const name of files) {
  if (typeof name !== 'string' || !name) continue;
  const file = path.resolve(absolute,name);
  if (path.dirname(file) !== absolute || !managed.test(path.basename(file))) continue;
  protectedFiles.add(file);
  const st = fs.lstatSync(file,{throwIfNoEntry:false});
  if (!st?.isFile() || time-st.atimeMs < 5000) continue;
  try { fs.utimesSync(file,time/1000,st.mtimeMs/1000); } catch (err) { if (err.code !== 'ENOENT') throw err; }
 }
 const rows=[];
 let size=0;
 for (const name of fs.readdirSync(absolute)) {
  if (!managed.test(name) || name.endsWith('.tmp')) continue;
  const file=path.join(absolute,name);
  const st=fs.lstatSync(file,{throwIfNoEntry:false});
  if (!st?.isFile()) continue;
  size+=st.size;
  const lastUse=Math.max(st.atimeMs,st.mtimeMs);
  if (!protectedFiles.has(file) && time-lastUse>handoffMs)
   rows.push({file,size:st.size,lastUse});
 }
 if (size<=budget) return;
 rows.sort((a,b)=>a.lastUse-b.lastUse);
 for (const row of rows) {
  if (size<=budget) break;
  // Concurrent helpers may have just refreshed or handed off an old entry.
  const st=fs.lstatSync(row.file,{throwIfNoEntry:false});
  if (!st) {size-=row.size;continue;}
  if (!st.isFile() || Math.max(st.atimeMs,st.mtimeMs)!==row.lastUse) continue;
  if (fs.existsSync(path.join(absolute,'.source-state',path.basename(row.file)+'.lock'))) continue;
  fs.rmSync(row.file,{force:true});
  size-=row.size;
 }
}
