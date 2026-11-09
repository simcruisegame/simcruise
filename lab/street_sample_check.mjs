// Offline regression for cache sharing, missing coverage and source timestamps.
// The synthetic catalog is deliberately independent of any named location.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {setTimeout as wait} from 'node:timers/promises';
import {selectSequence,streetSample} from '../helper/street-samples.mjs';

const [x,y]=[17400,25200];
const lat=Math.atan(Math.sinh(Math.PI*(1-2*(y+.5)/65536)))*180/Math.PI;
const lon=(x+.5)/65536*360-180;
const image=fs.readFileSync(new URL('../game/data/grass-detail.jpg',import.meta.url));
if(process.argv[2]==='worker'){
 const [directory,mode]=process.argv.slice(3);
 globalThis.fetch=async url=>{
  const list=new URL(url).pathname.startsWith('/1.0/');
  fs.appendFileSync(path.join(directory,'requests.log'),list?'catalog\n':'thumb\n');
  await wait(80);
  if(mode==='unavailable')return new Response('',{status:503,headers:{'Retry-After':'1'}});
  const photos=[0,1,2].map(i=>({id:String(i+10),sequence_id:'1200',sequence_index:i,projection:'PLANE',
   lat:lat+(i-1)*.00013,lng:lon,heading:'0',username:'fixture',shot_date:i===2?null:'2017-11-05 17:03:09.000',
   date_added:'2025-05-01 20:00:00',lth_name:'storage13/files/photo/2025/5/1/lth/1200_test'+i+'.jpg'}));
  return new Response(list?JSON.stringify({status:{apiCode:600},currentPageItems:mode==='empty'?[]:photos}):image,{status:200});
 };
 const sample=await streetSample(x,y,directory);
 process.stdout.write(JSON.stringify(sample));
}else{
 const center={lat,lon};
 function triplet(sequenceId,eastM,date){
  return [0,1,2].map(i=>({id:sequenceId+'-'+i,sequenceId,sequenceIndex:i,
   lat:lat+(i-1)*14/111195,lon:lon+eastM/(111195*Math.cos(lat*Math.PI/180)),heading:0,capturedAt:date}));
 }
 const old=triplet('old',0,'2016-06-01T12:00:00');
 const choose=(...groups)=>selectSequence(groups.flat(),center)[0]?.sequenceId;
 assert.equal(choose(old,triplet('recent',2,'2024-06-01T12:00:00')),'recent');
 assert.equal(choose(old,triplet('far',15,'2024-06-01T12:00:00')),'old');
 assert.equal(choose(triplet('unknown',0,null),triplet('known',15,'2024-06-01T12:00:00')),'unknown');
 const uploaded=triplet('uploaded',3,null).map(f=>({...f,date_added:'2026-06-01 12:00:00',timestamp:1780315200}));
 assert.equal(choose(old,uploaded),'old');
 assert.equal(choose(triplet('future',0,'2100-05-01T12:00:00'),triplet('recorded',0,'2018-06-01T12:00:00')),'recorded');
 console.log('Capture date breaks comparable geometry ties; closer and undated routes remain eligible; upload and future timestamps confer no recency.');
 const work=fs.mkdtempSync(path.join(os.tmpdir(),'sunday-street-check-'));
 const script=fileURLToPath(import.meta.url);
 const launch=(dir,mode)=>new Promise((resolve,reject)=>{
  const p=spawn(process.execPath,[script,'worker',dir,mode],{stdio:['ignore','pipe','inherit']});
  let output='';p.stdout.on('data',b=>output+=b);p.on('error',reject);
  p.on('close',code=>{if(code===0)resolve(JSON.parse(output));else reject(Error('Child exit '+code))});
 });
 try{
  for(const mode of ['available','empty','unavailable']){
   const dir=path.join(work,mode);fs.mkdirSync(dir);
   const [first,second]=await Promise.all([launch(dir,mode),launch(dir,mode)]);
   const warm=await launch(dir,mode);
   const expected=mode==='empty'?'no_coverage':mode;
   assert.equal(first.status,expected);assert.equal(second.status,expected);assert.equal(warm.status,expected);
   const log=fs.readFileSync(path.join(dir,'requests.log'),'utf8').trim().split('\n');
   assert.equal(log.filter(s=>s==='catalog').length,1);
   assert.equal(log.filter(s=>s==='thumb').length,mode==='available'?3:0);
   if(mode==='available'){
    assert.equal(first.frames.length,3);assert.equal(first.frames[2].capturedAt,null);
    assert.equal(first.frames[0].capturedAt,'2017-11-05T17:03:09.000');
    assert.equal(first.frames[0].author,'fixture');assert.equal(first.frames[0].sourceUrl,'https://kartaview.org/details/1200/0');
    assert(first.frames.every(f=>path.dirname(f.imagePath)===dir && fs.existsSync(f.imagePath)));
   }
   console.log(mode,first.status,log.length,'upstream calls across 2 simultaneous cold workers and 1 warm worker');
  }
 }finally{fs.rmSync(work,{force:true,recursive:true})}
}
