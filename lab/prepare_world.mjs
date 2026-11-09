import {cacheReader} from '../helper/atlas-cache.mjs';
import {canopy} from '../helper/canopy.mjs';
import {cartography} from '../helper/cartography.mjs';
import fs from 'node:fs';
import path from 'node:path';
const project=process.argv[2],cache=process.argv[3];const dir=path.join(project,'game/data');
const read=cacheReader(cache,()=>{});const normal=(k,url,ttl=30*86400000)=>read(k,url,ttl);
const cells=fs.readdirSync(dir).filter(n=>/^\d+_\d+.json$/.test(n));
let crownCount=0,area=0;
for(const f of cells){
 const t=JSON.parse(fs.readFileSync(path.join(dir,f)));
 if(!t.field||t.canopy==='lidar')continue;
 try{const c=await canopy(...t.id,read);t.crowns=c;t.canopy='CHMv2';t.trees=[];fs.writeFileSync(path.join(dir,f),JSON.stringify(t));crownCount+=c.length;area++}catch(e){console.error(f,e.message)}
}
console.log('Baked canopy',area,crownCount);
const wanted=new Set();
for(const [x,y] of [[18179,27907]]){
 for(let z=5;z<=13;z++){
  const span=2**(16-z),mx=Math.floor(x/span),my=Math.floor(y/span);
  // Center and surrounding overview tiles for the bundled Miami Beach start (the offline fallback).
  for(let i=-1;i<=1;i++)for(let j=-1;j<=1;j++)wanted.add([z,mx+i,my+j].join('_'));
 }
}
for(const key of wanted){
 const file=path.join(dir,'map-'+key+'.json');if(fs.existsSync(file))continue;
 try{fs.writeFileSync(file,JSON.stringify(await cartography(...key.split('_').map(Number),normal)))}catch(e){console.error(key,e.message)}
}
console.log('overview requested',wanted.size);
