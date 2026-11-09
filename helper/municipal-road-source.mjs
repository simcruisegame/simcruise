import {readRoadRings,roadClearanceMeters,observedStreetSection} from './esri-pavement.mjs';
import fs from 'node:fs';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {cacheReader} from './atlas-cache.mjs';
const layer='https://services1.arcgis.com/WnzC35krSYGuYov4/arcgis/rest/services/';
const W=40075016.68557849/65536;
const extent={xmin:-7921542,ymin:5213970,xmax:-7910824,ymax:5221671,spatialReference:{wkid:3857}};
const day=86400000;
function query(name,params){const u=new URL(layer+name+'/FeatureServer/0/query');u.search=new URLSearchParams(params);return u.href;}
const roadQuery=query('Roads',{f:'pbf',where:"TYPE in ('RD-PAVED','RD-UNPAVED','RD-ALLEY')",geometry:JSON.stringify(extent),geometryType:'esriGeometryEnvelope',inSR:'3857',outSR:'3857',outFields:'OBJECTID',resultType:'tile',returnGeometry:'true',resultRecordCount:'1024',quantizationParameters:JSON.stringify({mode:'view',originPosition:'upperLeft',tolerance:.24,extent})});
export async function curbsideClearance(x,y,rows,profiles,cache,onRequest,fetcher=fetch){
 const names=Object.keys(profiles);
 if(!names.length||!Number.isInteger(x)||!Number.isInteger(y))return {};
 const west=W*(x-32768),north=W*(32768-y),scale=1/Math.cosh(Math.PI*(1-2*(y+.5)/65536));
 if(west>extent.xmax||west+W<extent.xmin||north<extent.ymin||north-W>extent.ymax)return {};
 const valid=rows.filter(r=>r[11]==='cambridge-ma'&&r[10]==='matched'&&profiles[r[9]]?.source==='cambridge-2024');
 if(!valid.length)return {};
 const read=cacheReader(cache,onRequest,(u,o)=>fetcher(u,{...o,redirect:'error'}));
 const points=valid.map(r=>[Math.round((west+r[0]*W)*100)/100,Math.round((north-r[1]*W)*100)/100]);
 const multipoint=JSON.stringify({points,spatialReference:{wkid:3857}});
 if(points.length>72)return {};
 const k='civic-pits-v2-'+x+'-'+y+'-'+createHash('sha256').update(multipoint).digest('hex').slice(0,16);
 const miss=path.join(cache,k+'.miss');
 if(Date.now()-(fs.statSync(miss,{throwIfNoEntry:false})?.mtimeMs??0)<6*3600000)return {};
 const wells=query('Sidewalks',{f:'json',geometry:multipoint,geometryType:'esriGeometryMultipoint',inSR:'3857',outSR:'3857',where:"TYPE='TREE-WELL' AND TREE_IN_WELL='Y'",outFields:'OBJECTID,TYPE,TREE_IN_WELL',returnGeometry:'true',geometryPrecision:'2',resultRecordCount:'900'});
 let invalidKey=null;
 try{
  const wellBytes=await read(k,wells,90*day,null,5000,288*1024);
  invalidKey=k;
  const resp=JSON.parse(wellBytes);
  if(!Array.isArray(resp.features)||resp.exceededTransferLimit||resp.features.length>900)throw Error('Incomplete tree wells');
  invalidKey=null;
  const pits=[];
  for(const feature of resp.features){
   if(feature.attributes?.TYPE!=='TREE-WELL'||feature.attributes?.TREE_IN_WELL!=='Y'||!Number.isInteger(feature.attributes.OBJECTID)||feature.geometry?.rings?.length!==1)continue;
   const coords=feature.geometry.rings[0];
   if(coords.length<4||coords.length>150||coords.some(p=>!Array.isArray(p)||!p.slice(0,2).every(Number.isFinite)))continue;
   const ring=coords.map(([px,py])=>[(px-west)*scale,(north-py)*scale]);
   const area=Math.abs(ring.slice(1).reduce((sum,b,i)=>sum+ring[i][0]*b[1]-ring[i][1]*b[0],0))*.5;
   if(area<.7||area>32)continue;
   pits.push({id:feature.attributes.OBJECTID,ring,area});
  }
  const eligible=new Map();
  for(const row of valid){
   const p=[row[0]*W*scale,row[1]*W*scale];
   const support=pits.filter(pit=>inside(p,pit.ring));
   if(support.length!==1)continue;
   const pit=support[0];if(eligible.has(pit.id)){eligible.set(pit.id,null);continue;}
   eligible.set(pit.id,{row,pit});
  }
  if(!eligible.size)return {};
  const roadKey='civic-pavement-v1-cambridge-2018-q024';
  const roadBytes=await read(roadKey,roadQuery,90*day,null,6500,750*1024);
  invalidKey=roadKey;const road=readRoadRings(roadBytes);invalidKey=null;
  const output={};
  for(const pair of eligible.values()){
   if(!pair)continue;
   const {row,pit}=pair,p=[west+row[0]*W,north-row[1]*W];
   const clearance=roadClearanceMeters(p,road,scale);
   // Require the entire inventoried trunk radius plus a grid/simplification allowance.
   if(!Number.isFinite(clearance)||clearance<.32+Math.max(.24,row[4]*.5))continue;
   const section=observedStreetSection(p,road,scale);
   if(!section)continue;
   output[row[9]]={source:'cambridge-planimetric-2018',well:pit.id,pavementClearanceM:Math.floor(clearance*100)/100,...section};
  }
  return output;
 }catch{
  if(invalidKey)fs.rmSync(path.join(cache,invalidKey),{force:true});
  fs.writeFileSync(miss,'');return {};
 }
}
function inside(point,ring){
 let hit=false;
 for(let i=1;i<ring.length;i++){
  const a=ring[i-1],b=ring[i];
  if((a[1]>point[1])!==(b[1]>point[1])&&point[0]<a[0]+(point[1]-a[1])*(b[0]-a[0])/(b[1]-a[1]))hit=!hit;
 }return hit;
}
