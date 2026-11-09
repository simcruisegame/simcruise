// Visual-only relief beyond the active road tiles. Distances and heights are metres.
import fs from 'node:fs';
import path from 'node:path';
import Martini from '@mapbox/martini';
import {correctHeightOutliers} from './regional-height-outliers.mjs';
import {middleRoadMask} from './landscape-roads.mjs';
import {decode,encode} from 'fast-png';
import {oceanSampler} from './regional-ocean.mjs';

const gridVersion=4;
const earth = 40075016.68557849;
const radius = 45000;
const spokes = 512;
const rings = 128;
const day = 86400000;
const tau = 2 * Math.PI;

function lookup(maps, z, u, v) {
 const n = 2 ** z;
 const x = u * n * 256 - .5, y = v * n * 256 - .5;
 const ix = Math.floor(x), iy = Math.floor(y);
 function pixel(i, j) {
  const tx = Math.floor(i / 256), ty = Math.floor(j / 256);
  const data = maps.get(z + '/' + ((tx % n + n) % n) + '/' + ty);
  if (!data) return NaN;
  const off = ((j - ty * 256) * 256 + i - tx * 256) * data.channels;
  return data.data[off] * 256 + data.data[off + 1] + data.data[off + 2] / 256 - 32768;
 }
 const fx = x - ix, fy = y - iy;
 return (pixel(ix, iy) * (1 - fx) + pixel(ix + 1, iy) * fx) * (1 - fy)
    + (pixel(ix, iy + 1) * (1 - fx) + pixel(ix + 1, iy + 1) * fx) * fy;
}

function cell(z,u,v) {
 const n = 2 ** z;
 return [z, (Math.floor(u*n)%n+n)%n, Math.floor(v*n)];
}

async function fetchHeightTile(cell, maps, read) {
 const key = cell.join('/');
 if (maps.has(key)) return;
 const data = await read('relief-dem-' + cell.join('-'), 'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/' + key + '.png', 180 * day, null, 3300, 300000);
 if (data.length > 300000) throw Error('Unexpected terrain image size');
 const image = decode(data);
 if (image.width !== 256 || image.height !== 256 || image.channels < 3 || image.depth !== 8) throw Error('Unexpected terrain encoding');
 maps.set(key,image);
}

function skylineCells(maps,base,fine,u0,v0,metresPerWorld,budget) {
 const quadrantVotes=Array.from({length:4},()=>new Map());
 const ground=lookup(maps,base,u0,v0);
 for(let k=0;k<spokes;k++){
  const angle=k*tau/spokes, a=Math.sin(angle), b=-Math.cos(angle);
  let peak=null, maxSlope=0.004;
  for(let j=0;j<=rings;j++){
   const r=radius*(j/rings)**1.35, u=u0+a*r/metresPerWorld, v=v0+b*r/metresPerWorld;
   const h=lookup(maps,base,u,v);if(!Number.isFinite(h))throw Error('Incomplete distant terrain');
   if(r<5500)continue;
   const slope=(h-ground-r*r/12742000)/r;
   if(slope>maxSlope){maxSlope=slope;peak=cell(fine,u,v);}
  }
  if(peak){const peakVotes=quadrantVotes[Math.floor(k*4/spokes)],key=peak.join('/');peakVotes.set(key,(peakVotes.get(key)??0)+Math.min(.18,maxSlope));}
 }
 // Spend the refinement budget on the DEM cells that actually form high skyline
 // over many bearings. A flat district usually needs no extra data.
 const ranked=quadrantVotes.map(votes=>[...votes].filter(row=>row[1]>.06).sort((a,b)=>b[1]-a[1]));
 const preferred=new Set(ranked.flatMap(list=>list.length?[list[0][0]]:[]).slice(0,budget));
 for(const [key] of ranked.flat().sort((a,b)=>b[1]-a[1])){if(preferred.size>=budget)break;preferred.add(key);}
 return [...preferred];
}

function sampleSurface(maps,base,fine,u0,v0,extent,samples) {
 const fineN=2**fine;
 const span=samples-1, size=samples*samples;
 const surface=new Float32Array(size);
 const water=new Uint8Array(513*513);
 const oceanAt=oceanSampler();
 let seaSamples=0;
 for(let row=0;row<samples;row++){
  const v=v0+(2*row/span-1)*extent;
  const rowLatitude=Math.atan(Math.sinh(Math.PI*(1-2*v)))*180/Math.PI;
  for(let col=0;col<samples;col++){
   const u=u0+(2*col/span-1)*extent;
   let h=lookup(maps,base,u,v);
   if(!Number.isFinite(h))throw Error('Missing geographic sample');
   if(maps.has(cell(fine,u,v).join('/'))){
    const px=(u*fineN%1+1)%1,py=v*fineN%1;
    const t=Math.min(1,Math.min(px,1-px,py,1-py)*40);
    if(t>.01){
     const high=lookup(maps,fine,u,v);
     if(Number.isFinite(high))h+=(high-h)*t*t*(3-2*t);
    }
   }
   if(h<2 && oceanAt(360*u-180,rowLatitude)){
    h=0;seaSamples++;
    if(row%2===0&&col%2===0)water[(row/2)*513+col/2]=255;
   }
   surface[row*samples+col]=h;
  }
 }
 return {surface,water,seaSamples};
}

function encodeMesh(surface,samples) {
 const span=samples-1;
 const triangulator=new Martini(samples);
 const tile=triangulator.createTile(surface);
 let tolerance=4;
 let mesh=tile.getMesh(tolerance);
 while(mesh.triangles.length>420000){
  tolerance*=1.5;mesh=tile.getMesh(tolerance);
 }
 const vertexCount=mesh.vertices.length/2, indexCount=mesh.triangles.length;
 const xyz=new Float32Array(vertexCount*3);
 for(let i=0;i<vertexCount;i++){
  const col=mesh.vertices[i*2],row=mesh.vertices[i*2+1];
  xyz[i*3]=radius*(2*col/span-1);xyz[i*3+1]=surface[row*samples+col];xyz[i*3+2]=radius*(2*row/span-1);
 }
 const header=Buffer.alloc(8);
 header.writeUInt32LE(vertexCount,0);header.writeUInt32LE(indexCount,4);
 const geometry=Buffer.concat([header,Buffer.from(xyz.buffer),Buffer.from(mesh.triangles.buffer)]);
 return {geometry,vertexCount,indexCount,tolerance};
}

async function photograph(read,name,bounds,size){
  if(bounds.some(value=>Math.abs(value)>earth/2))return '';
  const params=new URLSearchParams({bbox:bounds.join(','),bboxSR:'3857',imageSR:'3857',size:size+','+size,format:'jpg',f:'image'});
  try{
   const jpg=await read(name,'https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/export?'+params,60*day,null,3600,size===1536?1150000:650000);
   if(jpg.length<=(size===1536?1150000:650000) && jpg[0]===255 && jpg[1]===216)return name;
  }catch{}
  return '';
 }

export async function regionalTerrain(x,y,cache,read) {
 if (![x,y].every(v=>Number.isInteger(v)&&v>=0&&v<8192)) throw Error('Invalid terrain region');
 const stem='relief-tin-v'+gridVersion+'-'+x+'-'+y;
 const manifest=stem+'.json';
 const manifestFile=path.join(cache,manifest);
 const u0=(x+.5)/8192, v0=(y+.5)/8192;
 // A cached mesh can outlive the individual input tiles. Reuse it until the
 // imagery refresh window. Shorter expiry gives incomplete images a retry.
 try{
  const stat=fs.statSync(manifestFile);
  const prior=JSON.parse(fs.readFileSync(manifestFile,'utf8'));
  const files=[prior.grid,prior.aerial,prior.middle,prior.seaMask,...prior.photos.map(p=>p.name)].filter(Boolean);
  const complete=prior.aerial && prior.middle && prior.photos.length>=prior.detail;
  const ttl=complete?28*day:30*60000;
  const meshBytes=8+12*prior.vertices+4*prior.indices;
  if(prior.region[0]===x&&prior.region[1]===y&&prior.grid===stem+'.bin'&&
     fs.statSync(path.join(cache,stem+'.bin'),{throwIfNoEntry:false})?.size===meshBytes&&
     Date.now()-stat.mtimeMs<ttl&&files.every(name=>name===path.basename(name)&&name.startsWith('relief-')&&fs.statSync(path.join(cache,name),{throwIfNoEntry:false})?.isFile())){
   const mask=middleRoadMask(cache,x,y,u0,v0,prior.metresPerWorld);
   return {...prior,...mask,manifest,reused:true};
  }
 }catch{/* Missing or incomplete local result; rebuild from geographic source tiles. */}
 const latitude=Math.atan(Math.sinh(Math.PI*(1-2*v0)));
 const metresPerWorld=earth*Math.cos(latitude);
 const extent=radius/metresPerWorld;
 const base=Math.abs(latitude)>1.0?9:10, fine=base+1;
 const n=2**base, maps=new Map(), coarse=[];
 // Adjacent pixels at provider boundaries are sampled from their actual tiles.
 for(let iy=Math.floor((v0-extent)*n-1/256);iy<=Math.floor((v0+extent)*n+1/256);iy++){
  for(let ix=Math.floor((u0-extent)*n-1/256);ix<=Math.floor((u0+extent)*n+1/256);ix++){
   coarse.push([base,(ix%n+n)%n,iy]);
  }
 }
 const deadline=Date.now()+11500;
 for(let i=0;i<coarse.length;i+=2) {
  if(Date.now()>deadline)throw Error('Distant source time budget');
  await Promise.all(coarse.slice(i,i+2).map(t=>fetchHeightTile(t,maps,read)));
 }
 const heightQA=await correctHeightOutliers(maps,base,u0,v0,extent,read,deadline);
 const detail=skylineCells(maps,base,fine,u0,v0,metresPerWorld,4-heightQA.attempts);
 for(let i=0;i<detail.length&&Date.now()<deadline;i+=2)
  await Promise.allSettled(detail.slice(i,i+2).map(key=>fetchHeightTile(key.split('/').map(Number),maps,read)));
 const samples=1025;
 const {surface,water,seaSamples}=sampleSurface(maps,base,fine,u0,v0,extent,samples);
 const {geometry,vertexCount,indexCount,tolerance}=encodeMesh(surface,samples);
 const left=earth*(u0-.5-extent),right=earth*(u0-.5+extent),top=earth*(.5-v0+extent),bottom=earth*(.5-v0-extent);
 const imageName='relief-aerial-v1-'+x+'-'+y+'.jpg';
 const image=await photograph(read,imageName,[left,bottom,right,top],512);
 const middleSpan=13000/metresPerWorld;
 const midWest=u0-middleSpan/2,midNorth=v0-middleSpan/2;
 const midBounds=[earth*(midWest-.5),earth*(.5-midNorth-middleSpan),earth*(midWest+middleSpan-.5),earth*(.5-midNorth)];
 const middleName='relief-middle-v1-'+x+'-'+y+'.jpg';
 const middle=await photograph(read,middleName,midBounds,1536);
 const photos=[];
 for(const key of detail){
  if(Date.now()>deadline)break;
  const [z,tx,ty]=key.split('/').map(Number);
  const n=2**z,step=earth/n;
  const west=tx*step-earth/2,north=earth/2-ty*step;
  const name='relief-detail-v1-'+z+'-'+tx+'-'+ty+'.jpg';
  const result=await photograph(read,name,[west,north-step,west+step,north],1024);
  if(result)photos.push({name,uv:[(west-left)/(right-left),(top-north)/(top-bottom),step/(right-left),step/(top-bottom)]});
 }
 const filename=stem+'.bin';
 const target=path.join(cache,filename),temp=target+'.'+process.pid+'.tmp';
 try{fs.writeFileSync(temp,geometry);fs.renameSync(temp,target);}
 finally{fs.rmSync(temp,{force:true});}
 let seaMask='';
 if(seaSamples){
  seaMask='relief-ocean-v1-'+x+'-'+y+'.png';
  const target=path.join(cache,seaMask),temp=target+'.'+process.pid+'.tmp';
  try{fs.writeFileSync(temp,encode({width:513,height:513,channels:1,data:water}));fs.renameSync(temp,target);}
  finally{fs.rmSync(temp,{force:true});}
 }
 const descriptor={heightCorrections:heightQA.corrected,verificationTiles:heightQA.attempts,seaMask,seaSamples,status:'available',region:[x,y],grid:filename,aerial:image,middle,photos,radius,vertices:vertexCount,indices:indexCount,maxError:tolerance,metresPerWorld,coarse:coarse.length,detail:maps.size-coarse.length};
 const descriptorTmp=manifestFile+'.'+process.pid+'.tmp';
 try{fs.writeFileSync(descriptorTmp,JSON.stringify(descriptor));fs.renameSync(descriptorTmp,manifestFile);}
 finally{fs.rmSync(descriptorTmp,{force:true});}
 return {...descriptor,...middleRoadMask(cache,x,y,u0,v0,metresPerWorld),manifest,reused:false};
}
