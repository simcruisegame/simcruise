import fs from 'node:fs';
import path from 'node:path';
import {encode} from 'fast-png';
import {ridgeBand} from './ridge-band.mjs';

const day=86400000,edge=384;
function lon(x){return x/2048*360-180;}
function lat(y){return Math.atan(Math.sinh(Math.PI*(1-2*y/2048)))*180/Math.PI;}
export async function summitSnow(x,y,date,cache,read) {
 if(![x,y].every(v=>Number.isInteger(v)&&v>=0&&v<2048)||!/^\d{4}-\d{2}-\d{2}$/.test(date))throw Error('Invalid snow district');
 const stamp=Date.parse(date+'T00:00:00Z');
 if(!Number.isFinite(stamp)||new Date(stamp).toISOString().slice(0,10)!==date||stamp>Date.now()||date<'2017-01-01')throw Error('Invalid date');
 const stem='summit-snow-v2-'+x+'-'+y+'-'+date,output=stem+'.png',meta=path.join(cache,stem+'.json');
 try {
  const old=JSON.parse(fs.readFileSync(meta,'utf8'));
  if(old.x===x&&old.y===y&&old.date===date){
   const age=Date.now()-fs.statSync(meta).mtimeMs;
   if(old.status==='unobserved'&&age<day)return old;
   if(old.file===output&&age<30*day&&fs.statSync(path.join(cache,output)).size>60)return old;
  }
 } catch {}
 const start=new Date(stamp-3*day).toISOString(),end=new Date(Math.min(Date.now(),stamp+2*day-1)).toISOString();
 const params=new URLSearchParams({collections:'sentinel-2-c1-l2a',bbox:[lon(x),lat(y+1),lon(x+1),lat(y)].join(','),datetime:start+'/'+end,limit:'9',
  fields:'id,properties.datetime,properties.eo:cloud_cover,properties.proj:epsg,properties.proj:code,assets.scl,assets.red'});
 const catalog=await read('snow-stac-v2-'+x+'-'+y+'-'+date,'https://earth-search.aws.element84.com/v1/search?'+params,day,null,4500,168000);
 const scenes=JSON.parse(catalog).features;
 if(!Array.isArray(scenes))throw Error('Missing satellite catalog');
 const eligible=scenes.filter(s=>s.assets?.scl&&s.properties?.datetime&&Math.abs(Date.parse(s.properties.datetime)-stamp)<4*day);
 const score=s=>Math.abs(Date.parse(s.properties.datetime.slice(0,10))-stamp)/day+Number(s.properties['eo:cloud_cover']??100)*.021;
 eligible.sort((a,b)=>score(a)-score(b));
 const packed=new Uint8Array(edge*edge*2),budget={bytes:0,calls:0};
 const used=[],rads=[],opticalBudget={bytes:0,calls:0};let valid=0,positives=0;
 for(const scene of eligible.slice(0,3)) {
  if(used.length>=2||budget.calls>=10||budget.bytes>=260000)break;
  let raster;
  try{raster=await ridgeBand(scene,'scl',x,y,read,budget);}catch{continue;}
  if(!raster)continue;
  let added=0;const claimed=[];
  for(let i=0;i<raster.data.length;i++){
   if(packed[i*2+1])continue;
   const k=raster.data[i];
   if(k!==4&&k!==5&&k!==6&&k!==11)continue;
   packed[i*2]=k===11?255:0;packed[i*2+1]=255;valid++;added++;if(k===11){positives++;claimed.push(i);}
  }
  if(claimed.length>240)rads.push({scene,claimed});
  if(added)used.push({id:scene.id,date:scene.properties.datetime.slice(0,10),spacing:raster.spacing,pixels:added});
  if(valid>packed.length*.42)break;
 }
 const base={x,y,date,valid,positives,scenes:used,rangeBytes:budget.bytes,rangeReads:budget.calls};
 if(valid<edge*edge*.1){
  const miss={...base,status:'unobserved'},tmp=meta+'.'+process.pid+'.tmp';
  try{fs.writeFileSync(tmp,JSON.stringify(miss));fs.renameSync(tmp,meta);}
  finally{fs.rmSync(tmp,{force:true});}
  return miss;
 }
 // The classification can call an entire mountainside snow. Measure the
 // dark exposed faces in the same acquisition with its calibrated red band.
 const rgba=new Uint8Array(edge*edge*4);
 for(let i=0;i<edge*edge;i++){rgba[4*i]=packed[2*i];rgba[4*i+3]=packed[2*i+1];}
 const reflectance=[];
 for(const {scene,claimed} of rads.sort((a,b)=>b.claimed.length-a.claimed.length)){
  const band=scene.assets?.red?.['raster:bands']?.[0];
  if(!(band?.scale>0)||!Number.isFinite(band.offset??0))continue;
  let field;
  try{field=await ridgeBand(scene,'red',x,y,read,opticalBudget);}catch{continue;}
  if(!field)continue;
  let pixels=0;
  for(const i of claimed){
   const dn=field.data[i];
   if(dn===(band.nodata??0))continue;
   rgba[i*4+1]=Math.round(Math.max(0,Math.min(1,dn*band.scale+(band.offset??0)))*255);
   rgba[i*4+2]=255;pixels++;
  }
  if(pixels)reflectance.push({id:scene.id,spacing:field.spacing,pixels});
 }
 base.reflectance=reflectance;
 base.opticalBytes=opticalBudget.bytes;
 base.opticalReads=opticalBudget.calls;
 const image=Buffer.from(encode({width:edge,height:edge,channels:4,data:rgba}));
 const file=path.join(cache,output),tmp=file+'.'+process.pid+'.tmp',tmpMeta=meta+'.'+process.pid+'.tmp';
 const result={...base,status:'available',file:output};
 try {
  fs.writeFileSync(tmp,image);fs.renameSync(tmp,file);
  fs.writeFileSync(tmpMeta,JSON.stringify(result));fs.renameSync(tmpMeta,meta);
 }finally{fs.rmSync(tmp,{force:true});fs.rmSync(tmpMeta,{force:true});}
 return result;
}
