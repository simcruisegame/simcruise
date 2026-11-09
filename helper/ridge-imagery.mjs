// Read the existing regional mesh to find visible relief near the actual camera heading.
import fs from 'node:fs';
import path from 'node:path';
const earth=40075016.68557849,day=86400000;
const clamp=(x,lo,hi)=>Math.max(lo,Math.min(hi,x));
export function visibleRidges(data,x,y,eyeX,eyeHeight,eyeZ,bearing) {
 if(data.length<8)return [];
 const count=data.readUInt32LE(0),indices=data.readUInt32LE(4);
 if(count>250000||count<3||data.length!==8+12*count+4*indices)return [];
 const u=(x+.5)/8192,v=(y+.5)/8192,world=earth/Math.cosh(Math.PI*(1-2*v));
 const peaks=Array.from({length:33},()=>({elevation:.012,key:null,weight:0}));
 for(let id=0;id<count;id++) {
  const offset=8+12*id,px=data.readFloatLE(offset),height=data.readFloatLE(offset+4),pz=data.readFloatLE(offset+8);
  const dx=px-eyeX,dz=pz-eyeZ,dist=Math.hypot(dx,dz);
  if(dist<4800||dist>41500)continue;
  const direction=Math.atan2(dx,-dz)*180/Math.PI;
  const diff=((direction-bearing+540)%360)-180;
  if(Math.abs(diff)>16.5)continue;
  const slot=Math.round(diff)+16;
  if(slot<0 || slot>=33)continue;
  const slope=(height-eyeHeight-dist*dist/12742000)/dist;
  if(slope<=peaks[slot].elevation)continue;
  const tx=Math.floor((u+px/world)*2048),ty=Math.floor((v+pz/world)*2048);
  peaks[slot]={elevation:slope,key:tx+','+ty,weight:clamp((slope-.009)/.08,.05,1)*(1-.45*Math.abs(diff)/16.5)};
 }
 const votes=new Map();
 for(const p of peaks)if(p.key)votes.set(p.key,(votes.get(p.key)||0)+p.weight);
 return [...votes.entries()].sort((a,b)=>b[1]-a[1]).filter(([,val])=>val>=1.2).slice(0,2).map(([key])=>key.split(',').map(Number));
}
export async function ridgeImagery(x,y,eyeX,eyeHeight,eyeZ,bearing,cache,read) {
 const vals=[x,y,eyeX,eyeHeight,eyeZ,bearing];
 if(!vals.every(Number.isFinite)||![x,y].every(v=>Number.isInteger(v)&&v>=0&&v<8192)||Math.max(Math.abs(eyeX),Math.abs(eyeZ))>14000||Math.abs(eyeHeight)>7000||bearing<0||bearing>=360)throw Error('Invalid ridge camera');
 const f=path.join(cache,'relief-tin-v4-'+x+'-'+y+'.bin');
 const st=fs.statSync(f,{throwIfNoEntry:false});
 if(!st?.isFile()||st.size>5000000)return {region:[x,y],expected:null,images:[]};
 const choices=visibleRidges(fs.readFileSync(f),...vals);
 const u=(x+.5)/8192,v=(y+.5)/8192,world=earth/Math.cosh(Math.PI*(1-2*v)),extent=45000/world;
 const images=[];
 for(const [tx,ty] of choices) {
  const step=earth/2048,west=tx*step-earth*.5,north=earth*.5-ty*step;
  if(west< -earth*.5||west+step>earth*.5||north>earth*.5||north-step< -earth*.5)continue;
  const name='ridge-aerial-v1-11-'+tx+'-'+ty+'.jpg';
  const params=new URLSearchParams({bbox:[west,north-step,west+step,north].join(','),bboxSR:'3857',imageSR:'3857',size:'1536,1536',format:'jpg',f:'image'});
  try {
   const bytes=await read(name,'https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/export?'+params,180*day,null,5500,1400000);
   if(bytes.length<3000||bytes[0]!==255||bytes[1]!==216)continue;
   images.push({name,cell:[tx,ty],uv:[(tx/2048-u+extent)/(extent*2),(ty/2048-v+extent)/(extent*2),1/(2048*extent*2),1/(2048*extent*2)]});
  } catch { /* Keep existing regional imagery outside successful cells. */ }
 }
 return {region:[x,y],expected:choices.length,images};
}
