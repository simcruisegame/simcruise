import fs from 'node:fs';
import {createLazPerf} from 'laz-perf';
import wasm from 'laz-perf/lib/node/laz-perf.wasm' with {type:'file'};
import projects from './lidar-projects.json' with {type:'json'};
const names=new Set(projects), aliases=new Map(),earth=40075016.68557849, day=86400000;
let decoder;
for(const name of names){
 const alias=name.replace(/^USGS_LPC_/,'').replace(/_LAS_\d{4}$/,'').toLowerCase();
 aliases.set(alias,aliases.has(alias)?null:name);
}
export function matchProject(rows){
 const found=[];
 for(const {attributes:a} of rows){
  if(!a||typeof a.workunit!=='string'||!(a.collect_end>0)||a.collect_end>Date.now())continue;
  const name=names.has(a.workunit)?a.workunit:aliases.get(a.workunit.toLowerCase());
  if(!name||!/^[-A-Za-z0-9_]+$/.test(name))continue;
  found.push({name,start:a.collect_start>0?new Date(a.collect_start).toISOString().slice(0,10):null,end:new Date(a.collect_end).toISOString().slice(0,10),quality:a.ql});
 }
 return found.sort((a,b)=>b.end.localeCompare(a.end))[0]??null;
}
export async function projectFor(x,y,read){
 const v=(y+.5)/65536,lat=Math.atan(Math.sinh(Math.PI*(1-2*v)))*180/Math.PI,lon=(x+.5)/65536*360-180;
 const params=new URLSearchParams({geometry:lon.toFixed(6)+','+lat.toFixed(6),geometryType:'esriGeometryPoint',inSR:'4326',spatialRel:'esriSpatialRelIntersects',outFields:'workunit,collect_start,collect_end,ql',returnGeometry:'false',f:'json',resultRecordCount:'32'});
 const bytes=await read('roof-wesm-'+x+'-'+y,'https://index.nationalmap.gov/arcgis/rest/services/3DEPElevationIndex/MapServer/24/query?'+params,30*day,null,5800,38000);
 const catalog=JSON.parse(bytes);
 if(catalog.exceededTransferLimit||!Array.isArray(catalog.features))throw Error('Survey index unavailable');
 return matchProject(catalog.features);
}
export async function decodePatch(bytes,worldX,worldY,cos,details){
 if(bytes.length<227||bytes.readUInt32LE(0)!==0x4653414c)throw Error('Invalid LAS signature');
 const format=bytes[104]&63,record=bytes.readUInt16LE(105);
 const count=bytes.readUInt32LE(107)||((bytes[24]===1&&bytes[25]>=4&&bytes.length>=375)?Number(bytes.readBigUInt64LE(247)):0);
 if(format>10||record<(format<6?20:30)||record>512||!Number.isSafeInteger(count)||count<1||count>320000||record*count>42000000)throw Error('Invalid LAS dimensions');
 const scale=[131,139,147].map(i=>bytes.readDoubleLE(i)),offset=[155,163,171].map(i=>bytes.readDoubleLE(i));
 if(!scale.every(x=>x>0&&x<=1)||!offset.every(Number.isFinite))throw Error('Invalid LAS transform');
 decoder??=createLazPerf({wasmBinary:fs.readFileSync(wasm)});
 const lp=await decoder, input=lp._malloc(bytes.length),point=lp._malloc(record);
 let file;
 try{
  lp.HEAPU8.set(bytes,input);file=new lp.LASZip();file.open(input,bytes.length);
  if(file.getCount()!==count||file.getPointLength()!==record||file.getPointFormat()!==format)throw Error('LAS reader mismatch');
  const result=new Float32Array(count*4), metadata=details?new Uint32Array(count):null;let used=0;
  for(let i=0;i<count;i++){
   file.getPoint(point);
   const buf=lp.HEAPU8,cl=format<6?buf[point+15]&31:buf[point+16];
   const withheld=format<6?buf[point+15]&128:buf[point+15]&4;
   if(withheld||!(cl===1||cl===2||cl===6))continue;
   if(metadata){
    const flags=buf[point+14],mask=format<6?7:15,shift=format<6?3:4;
    const field=point+(format<6?18:20),source=buf[field]|(buf[field+1]<<8);
    metadata[used/4]=(flags&mask)|(((flags>>shift)&mask)<<8)|(source<<16);
   }
   const ints=lp.HEAP32,p=point>>2;
   const dx=(ints[p]*scale[0]+offset[0]-worldX)*cos,dy=(worldY-(ints[p+1]*scale[1]+offset[1]))*cos;
   result[used++]=dx; result[used++]=dy; result[used++]=ints[p+2]*scale[2]+offset[2]; result[used++]=cl;
  }
  if(metadata)details.metadata=metadata.subarray(0,used/4);
  return result.subarray(0,used);
 } finally{file?.delete();lp._free(point);lp._free(input);}
}
export async function surveyPatch(x,y,subX,subY,ground,buildings,read) {
 if(![x,y].every(v=>Number.isInteger(v)&&v>=0&&v<65536)||![subX,subY].every(i=>Number.isInteger(i)&&i>=0&&i<4)||!Number.isFinite(ground)||Math.abs(ground)>6500)throw Error('Invalid roof district');
 const ux=(subX+.5)*.25,uy=(subY+.5)*.25,cos=1/Math.cosh(Math.PI*(1-2*(y+.5)/65536)),side=earth*cos/65536;
 const eligible=buildings.map((b,index)=>({b,index,r:Math.hypot(b[4]-ux,b[5]-uy)*side})).filter(o=>o.r<side*.26&&o.b[3]<.1).sort((a,b)=>a.r-b.r).slice(0,40);
 if(!eligible.length)return {status:'no-footprints',nodes:[],points:[],eligible:[]};
 const project=await projectFor(x,y,read);
 if(!project)return {status:'no-public-project',nodes:[],points:[],eligible};
 const base='https://usgs-lidar-public.s3.amazonaws.com/'+project.name+'/';
 const info=JSON.parse(await read('roof-ept-'+project.name,base+'ept.json',90*day,null,5200,85000));
 const bounds=info.bounds;
 if(!Array.isArray(bounds)||bounds.length!==6||!bounds.every(Number.isFinite)||String(info.srs?.horizontal)!=='3857'||info.dataType!=='laszip'||!Number.isInteger(info.span)||info.span<32||info.span>512)throw Error('Unsupported EPT projection');
 const width=bounds[3]-bounds[0];
 if(!(width>0)||Math.abs(width-(bounds[4]-bounds[1]))>1||Math.abs(width-(bounds[5]-bounds[2]))>1)throw Error('Unsupported EPT cube');
 const depth=Math.min(14,Math.max(1,Math.round(Math.log2(width*cos/info.span/1.05))));
 const north=earth*.5-y*earth/65536,west=x*earth/65536-earth*.5;
 function address(building,z,level){
  const size=width/2**level;
  const cell=[west+building[4]*earth/65536,north-building[5]*earth/65536,z].map((v,i)=>Math.floor((v-bounds[i])/size));
  return cell.some(k=>k<0||k>=2**level)?null:[level,...cell].join('-');
 }
 const choices=new Map();
 for(const {b,r} of eligible){
  const cell=address(b,ground+Math.min(25,Math.max(3,b[2])),depth);
  if(cell)choices.set(cell,(choices.get(cell)||0)+1/(1+r/40));
 }
 const leader=[...choices.entries()].sort((a,b)=>b[1]-a[1])[0]?.[0];
 const attempts=[],nodes=[],points=[],metadata=[];let spent=0,transient=false;
 const focus=eligible.find(({b})=>address(b,ground+Math.min(25,Math.max(3,b[2])),depth)===leader)?.b;
 const ranked=[];
 if(focus){
  const district=[...choices.keys()].sort((a,b)=>choices.get(b)-choices.get(a));
  // Height datums and ground slopes commonly straddle an octree Z boundary.
  // Request both sides when that happens, then add independent finer returns.
  for(const key of [leader,address(focus,ground-7,depth),address(focus,ground+Math.max(3,focus[2]),depth+1),...district,address(focus,ground,depth-1)])
   if(key&&!ranked.includes(key))ranked.push(key);
 }
 for(const key of ranked){
  if(attempts.length>=4||nodes.length>=3||spent>=2000000)break;
  if(spent>2080000)break;
  attempts.push(key);
  try{
   const bytes=await read('roof-laz-'+project.name+'-'+key,base+'ept-data/'+key+'.laz',180*day,null,5800,Math.min(1600000,2250000-spent));
   if(bytes.length>Math.min(1600000,2250000-spent))continue;
   spent+=bytes.length;
   const detail={};
   const cloud=await decodePatch(bytes,west,north,cos,detail);metadata.push(detail.metadata);
   nodes.push({key,bytes:bytes.length,points:cloud.length/4});points.push(cloud);
  }catch(error){
   if(!/returned 404$|exceeds byte budget/.test(error.message))transient=true;
   if(/returned 429|returned 503|requested a pause|concurrency limit/.test(error.message))break;
  }
 }
 return {status:nodes.length?'available':'insufficient',project,nodes,attempts,points,metadata,eligible,side,ground,transient};
}
