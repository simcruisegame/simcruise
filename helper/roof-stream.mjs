import fs from 'node:fs';
import path from 'node:path';
import {roadControls,storeSections} from './road-crossfall.mjs';
import {surveyPatch} from './roof-survey.mjs';
import {fitRoofPatch} from './roof-fit.mjs';
import {localBuildingTile} from './overture-buildings.mjs';

const day=86400000;
function write(file,data) {
 const temp=file+'.'+crypto.randomUUID()+'.tmp';
 try{fs.writeFileSync(temp,JSON.stringify(data));fs.renameSync(temp,file);}
 finally{fs.rmSync(temp,{force:true});}
}
export async function streamedRoofs(x,y,subX,subY,ground,directory,read){
 if(![x,y].every(n=>Number.isInteger(n)&&n>=0&&n<65536)||![subX,subY].every(n=>Number.isInteger(n)&&n>=0&&n<4)||!Number.isFinite(ground)||Math.abs(ground)>6500)throw Error('Invalid roof district');
 const base=localBuildingTile(x,y,directory);
 const control=roadControls(x,y,base.fingerprint,directory);
 const id=[x,y],patch=[subX,subY],key=(control?'roof-district-v2-'+control.roadSignature.slice(0,12)+'-'+control.prepared+'-':'roof-district-v1-')+[x,y,subX,subY,base.fingerprint].join('-'),file=path.join(directory,key);
 let previous;
 try{
  previous=JSON.parse(fs.readFileSync(file));
  if(previous.expires>Date.now()){
   if(control&&previous.crossfall?.runs){
    const marker=path.join(directory,'road-crossfall-v1-'+id.join('-')+'.ready');
    if(!fs.existsSync(marker))write(marker,1);
   }
   return {...previous,cached:true};
  }
 }catch{}
 const lock=path.join(directory,'.source-state',key+'.lock');
 try{fs.mkdirSync(lock);}catch(e){
  if(e.code!=='EEXIST')throw e;
  if(Date.now()-(fs.statSync(lock,{throwIfNoEntry:false})?.mtimeMs??0)>180000){
   fs.rmSync(lock,{recursive:true,force:true});
  }
  return {id,patch,status:'pending',profiles:[],expires:Date.now()+30000};
 }
 try{
  if(control&&!previous){
   const legacy=path.join(directory,'roof-district-v1-'+[x,y,subX,subY,base.fingerprint].join('-'));
   let old;
   try{old=JSON.parse(fs.readFileSync(legacy));}catch{}
   if(old?.expires>Date.now()){
    // An upgrade to an unexpired roof result must not make a network request.
    // Replay only its existing source cache. If pruned, the next ordinary roof refresh can try again.
    const result={...old};
    try{
     const cached=(key,_url,_ttl,_header,_time,cap)=>{
      if(!/^roof-(wesm|ept|laz)-[A-Za-z0-9_-]+$/.test(key))throw Error('Invalid survey source');
      const source=path.join(directory,key),stat=fs.statSync(source,{throwIfNoEntry:false});
      if(!stat?.isFile()||stat.size>cap)throw Error('Missing locally retained survey source');
      return fs.readFileSync(source);
     };
     const study=await surveyPatch(x,y,subX,subY,ground,base.rows,cached);
     if(study.project?.name===old.project?.name&&study.nodes?.length&&
       study.nodes.every(n=>old.nodes?.some(o=>o.key===n.key)))
      result.crossfall=storeSections(study,control,patch,directory,old.expires,write);
    }catch{/* Keep the original profiles until their ordinary expiry. */}
    write(file,result);return {...result,cached:true};
   }
  }
  return await buildDistrict(id,patch,ground,base,file,directory,read,previous,control);
 }finally{fs.rmSync(lock,{recursive:true,force:true});}
}
async function buildDistrict(id,patch,ground,base,file,directory,read,previous,control){
 const [x,y]=id,[subX,subY]=patch;
 const downloaded=async (id,...args)=>{
  if(!id.startsWith('roof-laz-'))return read(id,...args);
  const missing=path.join(directory,id+'.miss');
  if(Date.now()-(fs.statSync(missing,{throwIfNoEntry:false})?.mtimeMs??0)<30*day)throw Error('Map service returned 404');
  try{return await read(id,...args);}
  catch(e){if(/returned 404$/.test(e.message))write(missing,0);throw e;}
 };
 let result;
 try{
  const study=await surveyPatch(x,y,subX,subY,ground,base.rows,downloaded);
  const fitted=study.points.length?fitRoofPatch(study):{profiles:[],refusals:{}};
  result={id,patch,status:study.status,project:study.project??null,nodes:study.nodes,eligible:study.eligible.length,refusals:fitted.refusals,profiles:fitted.profiles};
  result.expires=Date.now()+(study.transient?600000:study.status==='available'?30*day:study.status==='insufficient'?day:7*day);
  if(control&&!study.transient){
   try{result.crossfall=storeSections(study,control,patch,directory,result.expires,write);}catch{/* Optional road detail does not invalidate successful roofs. */}
  }
 }catch{
  if(previous?.profiles?.length)return {...previous,cached:true,stale:true};
  result={id,patch,status:'unavailable',profiles:[],expires:Date.now()+600000};
 }
 write(file,result);
 return result;
}
