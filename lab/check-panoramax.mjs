import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {publicReferences,panoraReferences} from '../helper/panoramax.mjs';
const lon=-96.675,lat=40.808,x=Math.floor((lon+180)*65536/360),y=Math.floor((1-Math.asinh(Math.tan(lat*Math.PI/180))/Math.PI)*32768);
const record={id:'8889f6fa-34ff-4959-adb9-7d162c46b68c',collection:'6d2c342d-fbb9-4bbe-a028-ac6d21a612ba',
 geometry:{type:'Point',coordinates:[lon,lat]},providers:[{roles:['producer'],name:'Sample contributor'}],links:[{rel:'license',href:'https://creativecommons.org/licenses/by-sa/4.0/'}],
 properties:{datetime:'2025-08-19T13:41:00Z',license:'CC-BY-SA-4.0','quality:horizontal_accuracy':5,'view:azimuth':91,'pers:interior_orientation':{field_of_view:74},exif:{private:'private-example'}}};
const collection=features=>({type:'FeatureCollection',features});
const parsed=publicReferences(collection([record]),x,y);
assert.equal(parsed[0].heading,91);assert.equal(parsed[0].horizontalFov,74);
for(const change of [{license:'CC-BY-NC-4.0'},{datetime:'2025-08-19T13:41:00'},{'quality:horizontal_accuracy':45}]){
 assert.equal(publicReferences(collection([{...record,properties:{...record.properties,...change}}]),x,y).length,0);
}
assert.deepEqual(publicReferences(collection([{...record,links:[]}]),x,y),[]);
assert.deepEqual(publicReferences(collection([record]),x+5,y),[]);
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'sd-panora-'));let n=0;
try{
 const mock=async()=>new Response(JSON.stringify(collection([record])));
 const first=await panoraReferences(x,y,dir,()=>n++,mock);
 const warm=await panoraReferences(x,y,dir,()=>n++,()=>{throw Error('offline')});
 assert.deepEqual(first.references,warm.references);assert.equal(n,1);
 assert.equal(first.via,'provider');assert.equal(warm.via,'cache');assert.equal(first.status,'available');
 assert.deepEqual(publicReferences({features:[record],links:[]},x,y),parsed);
 assert.throws(()=>publicReferences({type:'Feature',features:[record]},x,y));
 assert.deepEqual(publicReferences({features:[{...record,providers:{}},{...record,links:null},record]},x,y),parsed);
 const failed=await panoraReferences(x,y,dir,()=>n++,()=>{throw Error('offline')},.9,.1);
 assert.equal(failed.status,'unavailable');assert.equal(failed.references.length,0);
 assert.equal(fs.readFileSync(path.join(dir,'panoramax-v2-'+x+'-'+y+'-8-8.json'),'utf8').includes('private-example'),false);
 console.log('PASS licensed, geographically bounded, timed observations, local cache and EXIF filtering');
}finally{fs.rmSync(dir,{recursive:true,force:true})}
