import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {referenceWidths} from './road-widths.mjs';

const day=86400000, lifetime=28*day, span=40075016.68557849/65536;
const host='https://services6.arcgis.com/Do88DoK2xjTUCXd1/ArcGIS/rest/services/OSM_NA_Highways/FeatureServer/0/query';
const major=new Set(['motorway','trunk','primary','secondary','tertiary']);
const nullableNo=v=>v==null||v===''||v==='no';
const group=(x,y)=>[x-(x%2),y-(y%2)];
const rawKey=(x,y)=>'lane-layout-v1-'+group(x,y).join('-');
function validId(x,y){return [x,y].every(n=>Number.isInteger(n)&&n>=0&&n<65536);}
function query(x,y){
 const [gx,gy]=group(x,y),half=span*32768;
 const bbox=[gx*span-half-40,half-(gy+2)*span-40,(gx+2)*span-half+40,half-gy*span+40];
 return host+'?'+new URLSearchParams({f:'json',where:"name IS NOT NULL AND oneway IN ('yes','-1','1') AND highway IN ('primary','secondary','tertiary','residential','unclassified','living_street','trunk','motorway')",
  inSR:'3857',outSR:'3857',geometryType:'esriGeometryEnvelope',geometry:bbox.join(','),outFields:'osm_id2,name,oneway,lanes,bridge,tunnel,layer,highway,junction',
  resultRecordCount:'280',returnGeometry:'true',maxAllowableOffset:'.2',geometryPrecision:'2'});
}
function decode(raw){
 const doc=JSON.parse(raw);
 if(doc.error||doc.exceededTransferLimit||!Array.isArray(doc.features)||doc.features.length>280)throw Error('Incomplete road layout coverage');
 const out=[];let points=0;
 for(const f of doc.features){
  const a=f.attributes,parts=f.geometry?.paths;
  if(!a||typeof a.name!=='string'||a.name.length>140||!/^\d{1,16}$/.test(a.osm_id2)||!Array.isArray(parts)||parts.length>128)throw Error('Invalid road observation');
  for(const pts of parts){
   if(!Array.isArray(pts)||pts.length<2||(points+=pts.length)>14000||pts.some(p=>!Array.isArray(p)||p.length!==2||!p.every(Number.isFinite)))throw Error('Invalid road geometry');
   out.push({name:a.name.toLowerCase(),way:String(a.osm_id2),single:a.lanes==='1'&&['1','-1','yes'].includes(a.oneway)&&nullableNo(a.bridge)&&nullableNo(a.tunnel)&&String(a.layer||0)==='0'&&!a.junction,pts});
  }
 }
 return out;
}
export async function fetchLaneDistrict(x,y,dir,read){
 if(!validId(x,y))return {status:'unavailable'};
 const key=rawKey(x,y),miss=path.join(dir,key+'.miss');
 const negative=fs.statSync(miss,{throwIfNoEntry:false});
 if(negative&&Date.now()-negative.mtimeMs<6*3600000)return {status:'unavailable'};
 try{
  const raw=await read(key,query(x,y),lifetime,null,3200,240000);
  let parsed;
  try{parsed=decode(raw);}catch(error){
   // A short HTTP 200 error, malformed response or truncated coverage should
   // retry after the miss cooldown rather than aging as a 28-day valid map.
   const file=path.join(dir,key);
   try{if(fs.readFileSync(file).equals(Buffer.from(raw)))fs.unlinkSync(file);}catch{}
   throw error;
  }
  return {status:'checked',ways:parsed.length};
 }catch{
  try{fs.writeFileSync(miss,'0');}catch{}
  return {status:'unavailable'};
 }
}
function geom(points){
 let length=0;
 const segments=[];
 for(let i=1;i<points.length;i++){
  const a=points[i-1],b=points[i],dx=b[0]-a[0],dy=b[1]-a[1],len=Math.hypot(dx,dy);
  if(len<.001)continue;
  segments.push({a,b,dx:dx/len,dy:dy/len,len,s:length});length+=len;
 }
 const bounds = points.reduce((b,p)=>[Math.min(b[0],p[0]),Math.min(b[1],p[1]),Math.max(b[2],p[0]),Math.max(b[3],p[1])],[Infinity,Infinity,-Infinity,-Infinity]);
 return {segments,length,bounds};
}
function at(line,s){
 for(const seg of line.segments)if(s<=seg.s+seg.len)return [seg.a[0]+seg.dx*(s-seg.s),seg.a[1]+seg.dy*(s-seg.s)];
 return line.segments.at(-1)?.b;
}
function projection(p,line){
 let best={distance:Infinity};
 for(const seg of line.segments){
  const t=Math.max(0,Math.min(seg.len,(p[0]-seg.a[0])*seg.dx+(p[1]-seg.a[1])*seg.dy));
  const d=Math.hypot(p[0]-seg.a[0]-t*seg.dx,p[1]-seg.a[1]-t*seg.dy);
  if(d<best.distance)best={distance:d,s:seg.s+t,seg};
 }
 return best;
}
function need(r){return major.has(r[0])&&Boolean(r[1])&&r[2]&&r[4]>=7.3&&r[5]!=='unpaved';}
// Signs each road's geometry and the reference width of its class (not its own
// width, which can be narrower: road-widths.mjs); SurveyOutcropGround.gd
// road_signature() is the same function.
export function laneRoadSignature(roads){
 const rows=roads.map(r=>Math.round((Object.hasOwn(referenceWidths,r[0])?referenceWidths[r[0]]:r[4])*100)+'|'+r[3].map(p=>Math.round(p[0]*1e6)+'/'+Math.round(p[1]*1e6)).join(',')).sort();
 return createHash('sha256').update(rows.join('\n')).digest('hex');
}
export function matchedLaneSpans(x,y,roads,obs){
 const cos=1/Math.cosh(Math.PI*(1-2*(y+.5)/65536)),side=span*cos,west=x*span-span*32768,north=span*32768-y*span;
 const shapes=roads.map(r=>geom(r[3].map(p=>[p[0]*side,p[1]*side])));
 const byName=new Map(),buckets=new Map();
 const cell=(x,y)=>x*1024+y;
 // Nearby overlapping map features protect parallel ways, junctions and transitions.
 shapes.forEach((line,index)=>{for(const seg of line.segments){
  for(let i=Math.floor((Math.min(seg.a[0],seg.b[0])-10)/24);i<=Math.floor((Math.max(seg.a[0],seg.b[0])+10)/24);i++)
   for(let j=Math.floor((Math.min(seg.a[1],seg.b[1])-10)/24);j<=Math.floor((Math.max(seg.a[1],seg.b[1])+10)/24);j++){
    const k=cell(i,j),set=buckets.get(k)??new Set();set.add(index);buckets.set(k,set);
   }
 }});
 const names=new Set(roads.filter(need).map(r=>r[2].toLowerCase()));
 for(const o of obs){
  if(!names.has(o.name))continue;
  const group=byName.get(o.name)??[];group.push({...o,line:geom(o.pts.map(p=>[(p[0]-west)*cos,(north-p[1])*cos]))});byName.set(o.name,group);
 }
 const spans=[];let checks=0,segmentChecks=0;
 for(let i=0;i<roads.length;i++){
  const road=roads[i],line=shapes[i];if(!need(road))continue;
  const matches=byName.get(road[2].toLowerCase())??[];if(!matches.some(o=>o.single))continue;
  let chain=[];
  function flush(){
   if(chain.length>=4){
    const chunks=[];
    for(const r of chain){
     const last=chunks.at(-1);
     if(last&&last.seg===r.seg){last.b=r.p;}else chunks.push({seg:r.seg,a:r.p,b:r.p});
    }
    for(const part of chunks){
     if(Math.hypot(part.b[0]-part.a[0],part.b[1]-part.a[1])<2.8)continue;
     const uv=p=>p.map(a=>Number((a/side).toFixed(8)));
     spans.push({road:i,way:chain[0].way,a:uv(part.a),b:uv(part.b)});
    }
   }
   chain=[];
  }
  for(let s=12;s<Math.floor(line.length)-10;s+=3){
   if(++checks>6500||spans.length>192)return null;
   const p=at(line,s),prev=at(line,s-3),next=at(line,s+3),dx=next[0]-prev[0],dy=next[1]-prev[1],len=Math.hypot(dx,dy);
   if(len<5.85||Math.min(p[0],p[1],side-p[0],side-p[1])<10){flush();continue;}
   const fitting=[];
   for(const o of matches){
    const [minX,minY,maxX,maxY]=o.line.bounds;
    if(p[0]<minX-1.55||p[1]<minY-1.55||p[0]>maxX+1.55||p[1]>maxY+1.55)continue;
    segmentChecks+=o.line.segments.length;
    if(segmentChecks>160000)return null;
    const hit=projection(p,o.line);if(hit.distance>1.55||Math.min(hit.s,o.line.length-hit.s)<10)continue;
    const a=at(o.line,hit.s-3),b=at(o.line,hit.s+3),vx=b[0]-a[0],vy=b[1]-a[1],vl=Math.hypot(vx,vy);
    if(vl>5.85&&Math.abs(dx*vx+dy*vy)/(len*vl)>.992)fitting.push(o);
   }
   if(fitting.length!==1||!fitting[0].single){flush();continue;}
   if([...buckets.get(cell(Math.floor(p[0]/24),Math.floor(p[1]/24)))??[]].some(j=>j!==i&&projection(p,shapes[j]).distance<10)){flush();continue;}
   if(chain.length&&chain.at(-1).way!==fitting[0].way)flush();
   chain.push({p,way:fitting[0].way,seg:projection(p,line).seg});
  }
  flush();
 }
 return spans.length<=192 ? spans : null;
}
export function cachedLaneLayout(x,y,roads,dir){
 if(!validId(x,y)||!roads.some(need))return null;
 const file=path.join(dir,rawKey(x,y));
 try{
  const stat=fs.statSync(file,{throwIfNoEntry:false});
  if(!stat||!stat.isFile()||stat.size>240000||Date.now()-stat.mtimeMs>lifetime)return null;
  const obs=decode(fs.readFileSync(file)),spans=matchedLaneSpans(x,y,roads,obs);
  if(spans===null)return null;
  return {version:1,status:'checked',snapshot:new Date(stat.mtimeMs).toISOString(),source:'OpenStreetMap contributors via Esri North America',roadSignature:laneRoadSignature(roads),id:[x,y],spans};
 }catch{return null;}
}
