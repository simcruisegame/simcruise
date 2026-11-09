// Correct isolated, severely contradictory Terrarium source pixels before TIN
// construction. The stored PNGs stay immutable. Uncorroborated relief survives.
import {decode} from 'fast-png';

const dimension=256;
const directions=[[4,0],[4,2],[3,3],[2,4],[0,4],[-2,4],[-3,3],[-4,2],
 [-4,0],[-4,-2],[-3,-3],[-2,-4],[0,-4],[2,-4],[3,-3],[4,-2]];
const wrap=(n,size)=>(n%size+size)%size;
const sortNumeric=(a,b)=>a-b;

function meters(image,x,y){
 const pos=(y*dimension+x)*image.channels;
 return image.data[pos]*256+image.data[pos+1]+image.data[pos+2]/256-32768;
}
function setMeters(image,x,y,h){
 const pos=(y*dimension+x)*image.channels;
 const bits=Math.round((h+32768)*256);
 image.data[pos]=bits>>16;
 image.data[pos+1]=(bits>>8)&255;
 image.data[pos+2]=bits&255;
}
function collar(grid,x,y){
 const values=directions.map(([dx,dy])=>grid[(y+dy)*dimension+x+dx]).sort(sortNumeric);
 return {middle:(values[7]+values[8])*.5,spread:values[12]-values[3]};
}
function extremeSources(maps,z,u0,v0,extent){
 const groups=new Map(),world=2**z;
 for(const [key,image] of maps){
  const [level,tx,ty]=key.split('/').map(Number);
  if(level!==z)continue;
  const grid=new Float32Array(dimension*dimension);
  for(let i=0;i<grid.length;i++)grid[i]=meters(image,i%dimension,i>>8);
  const left=Math.max(7,Math.floor(((u0-extent)*world-tx)*dimension)-1);
  const right=Math.min(248,Math.ceil(((u0+extent)*world-tx)*dimension)+1);
  const top=Math.max(7,Math.floor(((v0-extent)*world-ty)*dimension)-1);
  const bottom=Math.min(248,Math.ceil(((v0+extent)*world-ty)*dimension)+1);
  for(let y=top;y<=bottom;y++)for(let x=left;x<=right;x++){
   const i=y*dimension+x,center=grid[i];
   // Cheap opposite-axis test precedes the full sixteen-direction collar. A
   // hillside, long ridge or long shoreline usually fails this first pass.
   const dx=(grid[i-4]+grid[i+4])*.5-center;
   const dy=(grid[i-4*dimension]+grid[i+4*dimension])*.5-center;
   if(Math.min(dx,dy)<145)continue;
   const {middle,spread}=collar(grid,x,y);
   const gap=Math.abs(center-middle);
   if(spread>45 || gap<Math.max(180,6*spread))continue;
   // Reject mountainous neighborhoods and mixed land/bathymetry. This pass
   // targets deep inland pits; valid lower-than-sea-level basins survive.
   const wider=[];
   for(let dy=-7;dy<=7;dy+=2)for(let dx=-7;dx<=7;dx+=2)wider.push(grid[(y+dy)*dimension+x+dx]);
   wider.sort(sortNumeric);
   if(wider[6]<0 || wider[57]-wider[6]>65)continue;
   const fx=wrap(tx*4+(x>>6),world*4),fy=ty*4+(y>>6);
   const child=[z+2,fx,fy],id=child.join('/');
   let group=groups.get(id);
   if(!group){group={child,id,image,grid,points:[],score:0};groups.set(id,group);}
   group.points.push([x,y,middle]);
   group.score=Math.max(group.score,gap);
  }
 }
 return [...groups.values()].sort((a,b)=>b.score-a.score || a.id.localeCompare(b.id));
}

function fineSupport(image,x,y){
 // Each coarse pixel covers exactly four by four pixels at the witness zoom.
 const values=[];
 for(let dy=0;dy<4;dy++)for(let dx=0;dx<4;dx++)values.push(meters(image,(x%64)*4+dx,(y%64)*4+dy));
 values.sort(sortNumeric);
 return {middle:(values[7]+values[8])*.5,spread:values[12]-values[3]};
}
function repair(group,witness){
 const corrected=new Set();
 for(const [seedX,seedY,ring] of group.points){
  const sample=fineSupport(witness,seedX,seedY);
  const offset=group.grid[seedY*dimension+seedX]-sample.middle;
  // A second resolution must independently contain a smooth, collar-like
  // surface. Matching peaks, real pits and cliffs stay intact.
  if(sample.spread>12 || Math.abs(sample.middle-ring)>35 || Math.abs(offset)<95)continue;
  const blockX=(seedX>>6)*64,blockY=(seedY>>6)*64;
  for(let y=Math.max(seedY-4,blockY);y<=Math.min(seedY+4,blockY+63);y++){
   for(let x=Math.max(seedX-4,blockX);x<=Math.min(seedX+4,blockX+63);x++){
    const i=y*dimension+x;
    if(corrected.has(i))continue;
    const measured=fineSupport(witness,x,y);
    if(measured.spread>12 || Math.abs(measured.middle-ring)>45 ||
       Math.abs(group.grid[i]-measured.middle)<Math.max(14,4*measured.spread))continue;
    setMeters(group.image,x,y,measured.middle);
    corrected.add(i);
   }
  }
 }
 return corrected.size;
}

export async function correctHeightOutliers(maps,z,u0,v0,extent,read,deadline){
 const candidates=extremeSources(maps,z,u0,v0,extent);
 const result={attempts:0,corrected:0};
 // Leaves the former four-request refinement budget available for ordinary
 // skyline details in every district without convincing coarse candidates.
 for(const group of candidates.slice(0,2)){
  const remaining=deadline-Date.now();
  if(remaining<350)break;
  result.attempts++;
  try{
   const basename='relief-dem-'+group.child.join('-');
   const bytes=await read(basename,'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/'+group.id+'.png',
    180*86400000,null,Math.min(1600,remaining),300000);
   if(!bytes || bytes.length>300000)continue;
   const image=decode(bytes);
   if(image.width!==256 || image.height!==256 || image.channels<3 || image.depth!==8)continue;
   result.corrected+=repair(group,image);
  }catch{/* A throttled or unavailable witness cannot change ground elevations. */}
 }
 return result;
}
