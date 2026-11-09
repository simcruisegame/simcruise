import fs from 'node:fs';
import assert from 'node:assert/strict';
import {photoWeather,archiveSlice} from '../../helper/photo-weather.mjs';
import {satelliteWindow} from '../../helper/observed-cloud.mjs';
import {fuseSky} from '../../helper/sky-fusion.mjs';
const path=import.meta.dirname;
const json=n=>JSON.parse(fs.readFileSync(new URL(n+'.json',import.meta.url)));
const sw=(x,n)=>({available:true,good:49,clear:x,cloudy:49-x,delta_seconds:n});
const l=json('./fixtures/lincoln'),b=json('./fixtures/bartlesville'),chey=json('./fixtures/cheyenne'),mn=json('./evidence/minneapolis-prospective');
assert.equal(fuseSky(l,sw(49,2)).kind,'scattering');
assert.equal(fuseSky(b,sw(0,2)).kind,'broken_or_overcast');
assert.equal(fuseSky(chey,sw(49,0)).status,'insufficient');
assert.equal(fuseSky(mn,sw(49,0)).status,'insufficient');
assert.equal(fuseSky(l,sw(35,10)).status,'insufficient');
assert.equal(fuseSky(l,{...sw(49,1),good:25}).status,'insufficient');
assert.equal(fuseSky(l,sw(49,301)).status,'insufficient');
assert.equal(fuseSky(l,undefined).status,'unavailable');
const empty={status:'insufficient',reason:'no_bracketing_sky'};assert.equal(fuseSky(empty,sw(49,0)),empty);
assert.equal(satelliteWindow(61,-149,1755610860).product,'ACMF');
assert.equal(satelliteWindow(21,-158,1755610860).s,18);
assert.equal(satelliteWindow(40,-90,Date.UTC(2024,0,20)/1000).s,16);
assert.equal(satelliteWindow(40,-90,Date.UTC(2012,0,20)/1000),null);
let count=0; const fail=async()=>{count++;throw Error('should not request');};
for(const args of [[NaN,0,1755610860,0],[0,0,0,0],[44.408,-70.79333,1340995476,225],[40.8,-96.67,1755610860,7000]])assert.equal((await photoWeather(...args,fail)).status,'insufficient');
assert.equal(count,0);
const csv=fs.readFileSync(path+'/fixtures/lincoln.csv','utf8');
async function test(s){return photoWeather(40.808014,-96.675003,1755610860,374,()=>Buffer.from(s));}
for(const value of [csv.split('\n')[0],csv.replaceAll('2025-08-19','2025-08-18'),csv.replaceAll('CLR,M,M,M','M,M,M,M'),csv.replaceAll('-96.7633','-100.7633'),csv.replaceAll('-96.7633','bogus'),csv.replaceAll('CLR,M,M,M','VV,M,M,M')]){
 assert.equal((await test(value)).status,'insufficient');
}
assert.equal((await test('unavailable')).status,'unavailable');
assert.equal(archiveSlice(-91,0,0,0),null);
const idx=json('../../helper/photo-weather-stations');
const states=new Set(idx.map(v=>v[6]));assert.equal(idx.length,2677);
for(const state of ['AK','HI','FL','ND','NY','CA'])assert(states.has(state));
console.log('Archive selection, satellite coverage and abstention controls passed. '+idx.length+' stations / '+states.size+' jurisdictions.');

const {cacheReader}=await import('../../helper/atlas-cache.mjs');
const os=await import('node:os'), paths=await import('node:path');
const scratch=fs.mkdtempSync(paths.join(os.tmpdir(),'sd-archive-backoff-'));
try{
 let attempts=0;
 const reader=cacheReader(scratch,()=>attempts++,async()=>new Response(null,{status:429,headers:{'retry-after':'300'}}));
 const at=(key)=>reader(key,'https://mesonet.agron.iastate.edu/cgi-bin/request/asos.py',86400,null,5000,1024);
 await assert.rejects(at('metar-test1'));
 await assert.rejects(at('metar-test2'));
 assert.equal(attempts,1);
 const expires=Number(fs.readFileSync(paths.join(scratch,'.source-state','mesonet.agron.iastate.edu.until')));
 assert(expires>Date.now()+290000);
}finally{fs.rmSync(scratch,{recursive:true,force:true});}
