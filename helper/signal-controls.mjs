// Official Overture base display tiles publish the location of a signalized
// crossing. The source does not publish the individual poles or their phases.
import fs from 'node:fs';
import path from 'node:path';
import {gunzipSync} from 'node:zlib';
import {archiveTile} from './overture-archive.mjs';
import {vectorTile} from './vector-tile.mjs';

const day=86400000;
const catalog='https://stac.overturemaps.org/catalog.json';
const limit=768*1024;
const maxSignals=1600;
const osmNode=/^n[1-9]\d*@\d+$/;
const result=(status,records=[],release=null)=>({status,records,release});

export function observedControl(properties) {
 if(properties.class!=='traffic_signals'||properties.subtype!=='transportation')return null;
 let sources,tags;
 try {sources=JSON.parse(properties.sources);tags=JSON.parse(properties.source_tags);}
 catch{return null;}
 if(!Array.isArray(sources)||sources.length!==1||!tags||tags.highway!=='traffic_signals')return null;
 const root=sources[0];
 if(root?.property!==''||root.dataset!=='OpenStreetMap'||root.license!=='ODbL-1.0'||!osmNode.test(root.record_id))return null;
 // Signals separately attached to an OSM way approach are not proof that
 // all traffic at the nearby crossing is controlled.
 if(tags['traffic_signals:direction']||tags.direction||tags.level||Number(tags.layer??0)!==0||tags.railway||tags.barrier||tags.crossing==='uncontrolled')return null;
 if(tags.traffic_signals&&!['signal','traffic_lights'].includes(tags.traffic_signals))return null;
 return root.record_id;
}

function extract(layer) {
 if(!layer)return [];
 if(layer.length>160000)throw Error('Oversized infrastructure layer');
 const rows=[];const seen=new Set();
 for(let i=0;i<layer.length;i++){
  const f=layer.feature(i);if(f.type!==1)continue;
  const id=observedControl(f.properties);if(!id||seen.has(id))continue;
  const p=f.loadGeometry();
  if(p.length!==1||p[0].length!==1)continue;
  const u=p[0][0].x/f.extent,v=p[0][0].y/f.extent;
  if(u<0||u>=1||v<0||v>=1)continue;
  seen.add(id);rows.push([u,v,id]);
  if(rows.length>maxSignals)throw Error('Too many signal controls');
 }
 return rows;
}

export function ownedControls(rows,x,y) {
 const records=[];
 for(const row of rows){
  if(!Array.isArray(row)||row.length!==3||!Number.isFinite(row[0])||!Number.isFinite(row[1])||!osmNode.test(row[2]))continue;
  const u=row[0]*8-(x%8),v=row[1]*8-(y%8);
  if(u>=0&&u<1&&v>=0&&v<1)records.push({uv:[u,v],osm:row[2]});
 }
 return records.length<=64?records:[];
}

// Grade-separated crossings lack a reliable curb location in the display tile.
// Inspect the motor basemap already cached by the ordinary road request.
export function surfaceControls(records,layer,x,y) {
 if(!layer||records.length===0)return [];
 const tileSide=611.496/Math.cosh(Math.PI*(1-2*(y+.5)/65536));
 const unsafe=new Set();
 for(let i=0;i<layer.length;i++){
  const f=layer.feature(i),p=f.properties;
  if(f.type!==2||(!p.brunnel&&Number(p.layer??0)===0))continue;
  const extent=f.extent/4, radius=8*extent/tileSide, box=f.bbox();
  const nearby=records.map(record=>({record,px:(x%4+record.uv[0])*extent,py:(y%4+record.uv[1])*extent}))
   .filter(({px,py})=>px>=box[0]-radius&&px<=box[2]+radius&&py>=box[1]-radius&&py<=box[3]+radius);
  if(!nearby.length)continue;
  for(const line of f.loadGeometry())for(let j=1;j<line.length;j++){
   const a=line[j-1],b=line[j],dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
   for(const {record,px,py} of nearby){
    const t=length?Math.max(0,Math.min(1,((px-a.x)*dx+(py-a.y)*dy)/length)):0;
    if(Math.hypot(px-a.x-t*dx,py-a.y-t*dy)<=radius)unsafe.add(record);
   }
  }
 }
 return records.filter(record=>!unsafe.has(record));
}

export async function signalControls(x,y,cache,read) {
 if(!Number.isInteger(x)||!Number.isInteger(y)||x<0||y<0||x>=65536||y>=65536)return result('unsupported');
 const mx=x>>3,my=y>>3,miss=path.join(cache,'ov-controls-miss-'+mx+'-'+my);
 if(Date.now()-(fs.statSync(miss,{throwIfNoEntry:false})?.mtimeMs??0)<15*60000)return result('unavailable');
 try{
  const release=JSON.parse(await read('ov-catalog-v1',catalog,day,null,2200,128*1024)).latest;
  if(!/^20\d\d-\d\d-\d\d\.\d{1,3}$/.test(release))throw Error('Invalid release');
  const parsed=path.join(cache,'ov-controls-v1-'+release+'-'+mx+'-'+my);
  let rows;
  const stat=fs.statSync(parsed,{throwIfNoEntry:false});
  if(stat?.isFile()&&stat.size<=128*1024){
   try{rows=JSON.parse(fs.readFileSync(parsed,'utf8'));}catch{}
  }
  if(!Array.isArray(rows)||rows.length>maxSignals){
   let used=0,calls=0;
   const bounded=async(key,url,ttl,range,timeout)=>{
    const n=range[1]-range[0]+1;
    used+=n;
    if(++calls>5||used>limit)throw Error('Signal archive budget exceeded');
    return read(key,url,ttl,range,timeout,n);
   };
   const tile=await archiveTile(mx,my,release,bounded,'base');
   rows=tile?extract(vectorTile(tile).layers.infrastructure):[];
   const temp=parsed+'.'+process.pid+'.tmp';
   fs.writeFileSync(temp,JSON.stringify(rows));fs.renameSync(temp,parsed);
  }
  const owned=ownedControls(rows,x,y);
  if(!owned.length)return result('empty',[],release);
  const vector=fs.readFileSync(path.join(cache,'v-'+(x>>2)+'-'+(y>>2)));
  const raw=vector[0]===31?gunzipSync(vector,{maxOutputLength:10*1024*1024}):vector;
  const records=surfaceControls(owned,vectorTile(raw).layers.transportation,x,y);
  return result(records.length?'available':'empty',records,release);
 }catch{
  try{fs.writeFileSync(miss,'');}catch{}
  return result('unavailable');
 }
}
