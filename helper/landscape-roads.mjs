// Paint only the motor road corridors already present in detailed cached OSM
// tiles. The distant material has no collision, labels or invented lane marks.
import fs from 'node:fs';
import path from 'node:path';
import {gunzipSync} from 'node:zlib';
import {encode} from 'fast-png';
import {vectorTile} from './vector-tile.mjs';

const WIDTHS={motorway:19,trunk:16,primary:12,secondary:11,tertiary:10};
const EDGE=2048;
export function middleRoadMask(cache,x,y,u0,v0,metresPerWorld) {
 const span=13000/metresPerWorld, scale=EDGE/span;
 const west=u0-span/2,north=v0-span/2;
 const pixels=new Uint8Array(EDGE*EDGE);
 const pxMetres=13000/EDGE;
 let segments=0,readBytes=0;
 const present=[];
 for(let ty=Math.floor(north*16384);ty<=Math.floor((north+span)*16384);ty++){
  for(let tx=Math.floor(west*16384);tx<=Math.floor((west+span)*16384);tx++){
   const key='v-'+((tx+16384)%16384)+'-'+ty,file=path.join(cache,key);
   const stat=fs.statSync(file,{throwIfNoEntry:false});
   if(!stat||!stat.isFile()||stat.size>650000||stat.size+readBytes>2500000)continue;
   readBytes+=stat.size;
   let layer;
   try{
    const raw=fs.readFileSync(file);
    layer=vectorTile(raw[0]===31?gunzipSync(raw,{maxOutputLength:5000000}):raw).layers.transportation;
   }catch{continue;}
   if(!layer)continue;
   present.push(key);
   for(let i=0;i<layer.length;i++){
    const feature=layer.feature(i),p=feature.properties;
    const w=WIDTHS[p.class];
    if(!w||feature.type!==2||p.brunnel||Number(p.layer??0)!==0||p.surface==='unpaved')continue;
    const regular=w/(2*pxMetres),radius=regular+.65;
    for(const line of feature.loadGeometry()){
     for(let j=1;j<line.length;j++){
      const a=line[j-1],b=line[j],ext=feature.extent;
      const ax=((tx+a.x/ext)/16384-west)*scale,ay=((ty+a.y/ext)/16384-north)*scale;
      const dx=(b.x-a.x)*scale/(16384*ext),dy=(b.y-a.y)*scale/(16384*ext);
      const length2=dx*dx+dy*dy;
      if(length2<=.001)continue;
      const left=Math.max(0,Math.floor(Math.min(ax,ax+dx)-radius)),right=Math.min(EDGE-1,Math.ceil(Math.max(ax,ax+dx)+radius));
      const top=Math.max(0,Math.floor(Math.min(ay,ay+dy)-radius)),bottom=Math.min(EDGE-1,Math.ceil(Math.max(ay,ay+dy)+radius));
      if(right<left||bottom<top)continue;
      for(let row=top;row<=bottom;row++)for(let col=left;col<=right;col++){
       const ox=col+.5-ax,oy=row+.5-ay,t=Math.max(0,Math.min(1,(ox*dx+oy*dy)/length2));
       const weight=Math.max(0,Math.min(1,regular+.5-Math.hypot(ox-t*dx,oy-t*dy)));
       const idx=row*EDGE+col;
       pixels[idx]=Math.max(pixels[idx],Math.round(weight*255));
      }
      segments++;
     }
    }
   }
  }
 }
 if(!segments)return {roadMask:'',roadSegments:0,roadTiles:present.length};
 const name='relief-road-v1-'+x+'-'+y+'.png',target=path.join(cache,name),temp=target+'.'+process.pid+'.tmp';
 const bytes=encode({width:EDGE,height:EDGE,channels:1,data:pixels});
 try {fs.writeFileSync(temp,bytes);fs.renameSync(temp,target);} finally {fs.rmSync(temp,{force:true});}
 return {roadMask:name,roadSegments:segments,roadTiles:present.length};
}
