// Optional building-use estimates from public USACE NSI, strictly inside
// mapped ground footprints. Height, roof shape and facade materials stay with
// their existing sources. No raw USACE response is written to disk.
import fs from 'node:fs';
import path from 'node:path';
import {classifyRings} from '@mapbox/vector-tile';
import {localBuildingTile} from './overture-buildings.mjs';
import {cacheReader} from './atlas-cache.mjs';

const endpoint='https://nsi.sec.usace.army.mil/nsiapi/structures';
const day=86400000, maxBytes=768*1024, maxRows=1024;
const generic=new Set(['','building','yes']);
const lon=x=>x/65536*360-180;
const lat=y=>180/Math.PI*Math.atan(Math.sinh(Math.PI*(1-2*y/65536)));

function occupancy(value){
 if(typeof value!=='string')return null;
 if(/^RES1(?:-(?:[1-9][0-9]?S|SL)(?:NB|WB))?$/.test(value))return 'single_family';
 if(value==='RES3A')return 'duplex';
 if(/^RES3[B-F]$/.test(value))return 'multi_family';
 if(/^(?:COM(?:[1-9]|10)|IND[1-6]|AGR1|REL1|GOV[12]|EDU[12])$/.test(value))return 'nonresidential';
 return null;
}

function minimalPoints(doc,x,y){
 if(doc.type!=='FeatureCollection'||!Array.isArray(doc.features)||doc.features.length>maxRows)throw Error('Invalid structure district');
 return doc.features.map(f=>{
  const [east,north]=f.geometry?.coordinates??[],p=f.properties;
  if(f.geometry?.type!=='Point'||!Number.isFinite(east)||!Number.isFinite(north)||!p)throw Error('Invalid structure point');
  const u=(east+180)/360*65536-x,v=(1-Math.asinh(Math.tan(north*Math.PI/180))/Math.PI)*32768-y;
  if(!Number.isFinite(u)||!Number.isFinite(v))throw Error('Invalid structure coordinates');
  const stories=Number.isFinite(p.num_story)&&p.num_story>=.5&&p.num_story<=150?p.num_story:null;
  // Unsupported occupancy remains an ambiguity blocker for stacked records.
  return [u,v,occupancy(p.occtype),stories];
 });
}

function bounds(ring){
 const b=[Infinity,Infinity,-Infinity,-Infinity];
 for(const [x,y] of ring){b[0]=Math.min(b[0],x);b[1]=Math.min(b[1],y);b[2]=Math.max(b[2],x);b[3]=Math.max(b[3],y)}
 return b;
}

function crossingFootprints(layer,x,y){
 const blocks=[];
 if(!layer)return blocks;
 if(layer.length>15000)throw Error('Complex vector layer');
 for(let i=0;i<layer.length;i++){
  const f=layer.feature(i);if(f.type!==3)continue;
  const q=f.extent/4,sx=x%4,sy=y%4,b=f.bbox();
  if(b[0]>(sx+1)*q||b[2]<sx*q||b[1]>(sy+1)*q||b[3]<sy*q)continue;
  for(const poly of classifyRings(f.loadGeometry())){
   const rings=poly.map(r=>r.slice(0,-1).map(p=>[p.x/q-sx,p.y/q-sy]));
   const a=rings[0];if(a.length<3)continue;
   const c=a.reduce((s,p)=>[s[0]+p[0]/a.length,s[1]+p[1]/a.length],[0,0]);
   const obstructs=f.properties.hide_3d||f.properties['building:part']||c.some(v=>v<0||v>=1);
   if(obstructs){
    const box=bounds(a);
    if(box[2]>=0&&box[3]>=0&&box[0]<=1&&box[1]<=1)blocks.push({rings,box,index:-1});
   }
  }
 }
 return blocks;
}

// 2 = interior with >margin clearance, 1 = within the uncertain boundary band.
function relation(shape,p,margin){
 const [x,y]=p,[l,t,r,b]=shape.box;
 if(x<l-margin||x>r+margin||y<t-margin||y>b+margin)return 0;
 let inside=false;
 for(const ring of shape.rings)for(let i=0,j=ring.length-1;i<ring.length;j=i++){
  const a=ring[j],c=ring[i],dx=c[0]-a[0],dy=c[1]-a[1],u=x-a[0],v=y-a[1];
  const length2=dx*dx+dy*dy,z=length2?Math.max(0,Math.min(1,(u*dx+v*dy)/length2)):0;
  if((u-z*dx)**2+(v-z*dy)**2<=margin*margin)return 1;
  if((a[1]>y)!==(c[1]>y)&&x<a[0]+dx*v/dy)inside=!inside;
 }
 return inside?2:0;
}

export function matchedBuildingUses(rows,points,tags,margin,blocks=[]){
 if(rows.length+blocks.length>maxRows||points.length>maxRows||tags.size>maxRows)return [];
 const shapes=rows.map((r,index)=>({rings:r[0],box:bounds(r[0][0]),index}));
 shapes.push(...blocks);
 if(shapes.reduce((sum,s)=>sum+s.rings.reduce((n,r)=>n+r.length,0),0)>22000)return [];
 const assigned=Array.from({length:rows.length},()=>({nsi:[],osm:[],uncertain:false}));
 function assign(p,item,key){
  const hits=[];
  for(const shape of shapes){
   const hit=relation(shape,p,margin);
   if(hit)hits.push([shape.index,hit]);
  }
  for(const [idx,hit] of hits)if(idx>=0){
   if(hits.length!==1||hit!==2)assigned[idx].uncertain=true;
   else assigned[idx][key].push(item);
  }
 }
 for(const point of points)assign(point,point,'nsi');
 for(const tag of tags.values())if(tag&&tag.center?.length===2)assign(tag.center,tag,'osm');
 const uses=[];
 for(const [i,r] of rows.entries()){
  const a=assigned[i],b=shapes[i].box;
  if(a.uncertain||a.nsi.length!==1||a.osm.length!==1||r[3]!==0)continue;
  // A query clipped at a tile edge cannot establish sole membership there.
  if(b[0]<margin||b[1]<margin||b[2]>1-margin||b[3]>1-margin)continue;
  if(!generic.has(r[6]??'')||!generic.has(r[7]??''))continue;
  const tag=a.osm[0],point=a.nsi[0];
  if(!['building','yes'].includes(tag.building)||!point[2])continue;
  if(['usage','building_use','amenity','shop','office','tourism','historic','man_made','religion','leisure','landuse'].some(k=>tag[k]!=null&&tag[k]!==''))continue;
  uses.push({baseIndex:i,center:[r[4],r[5]],use:point[2],stories:point[3],source:'USACE NSI',basis:'modeled occupancy'});
 }
 return uses;
}

async function filteredResponse(url,opts,fetcher,x,y,stats){
 const res=await fetcher(url,{...opts,redirect:'error'});
 if(!res.ok){await res.body?.cancel();return new Response(null,{status:res.status,headers:res.headers})}
 if(!res.body||Number(res.headers.get('content-length'))>maxBytes){await res.body?.cancel();throw Error('Structure response exceeds budget')}
 const reader=res.body.getReader(),chunks=[];
 try{
  for(;;){
   const {done,value}=await reader.read();if(done)break;
   stats.bytes+=value.length;
   if(stats.bytes>maxBytes){await reader.cancel();throw Error('Structure response exceeds budget')}
   chunks.push(value);
  }
 }finally{reader.releaseLock()}
 const points=minimalPoints(JSON.parse(Buffer.concat(chunks).toString()),x,y);
 return new Response(JSON.stringify(points),{headers:{'content-type':'application/json'}});
}

export async function buildingUses(x,y,cache,fetcher=fetch,baseline=null){
 const stats={requests:0,bytes:0};
 const empty=status=>({status,uses:[],...stats});
 if(![x,y].every(v=>Number.isInteger(v)&&v>=0&&v<65536))return empty('unavailable');
 const key='nsi-uses-v1-'+x+'-'+y,pause=path.join(cache,key+'.pause');
 try{
  const base=baseline??localBuildingTile(x,y,cache,fetcher);
  if(!base.rows.length||base.rows.length>maxRows)return empty('empty');
  if(!base.rows.some(r=>r[3]===0&&generic.has(r[6]??'')&&generic.has(r[7]??'')))return empty('empty');
  if(Date.now()-(fs.statSync(pause,{throwIfNoEntry:false})?.mtimeMs??0)<90000)return empty('unavailable');
  const poly=[[x,y],[x+1,y],[x+1,y+1],[x,y+1],[x,y]];
  const bbox=poly.flatMap(([u,v])=>[lon(u).toFixed(7),lat(v).toFixed(7)]).join(',');
  const read=cacheReader(cache,()=>stats.requests++,(url,opts)=>filteredResponse(url,opts,fetcher,x,y,stats));
  const points=JSON.parse((await read(key,endpoint+'?fmt=fc&bbox='+bbox,30*day,null,2600)).toString());
  if(!points.length||!points.some(p=>p[2]))return empty('empty');
  const tags=await base.getBuildingTags();
  if(!tags||!tags.size)return empty('empty');
  const scale=40075016.68557849/65536*Math.cos(lat(y+.5)*Math.PI/180);
  const uses=matchedBuildingUses(base.rows,points,tags,1/scale,crossingFootprints(base.layer,x,y));
  return {status:uses.length?'available':'empty',uses,...stats};
 }catch{
  try{fs.writeFileSync(pause,'')}catch{}
  return empty('unavailable');
 }
}
