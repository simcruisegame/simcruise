import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {observedControl,ownedControls,signalControls,surfaceControls} from '../helper/signal-controls.mjs';

const tags={highway:'traffic_signals'};
const origin={property:'',dataset:'OpenStreetMap',license:'ODbL-1.0',record_id:'n140690065@9'};
const feature=(t,sources=[origin])=>({class:'traffic_signals',subtype:'transportation',source_tags:JSON.stringify(t),sources:JSON.stringify(sources)});
assert.equal(observedControl(feature(tags)),origin.record_id);
for(const extra of [{traffic_signals:'pedestrian_crossing'},{'traffic_signals:direction':'forward'},{layer:1},{railway:'level_crossing'},{barrier:'gate'}]){
 assert.equal(observedControl(feature({...tags,...extra})),null);
}
assert.equal(observedControl(feature(tags,[origin,origin])),null);
assert.equal(observedControl(feature(tags,[{...origin,license:null}])),null);
assert.equal(observedControl(feature(tags,[{...origin,record_id:'w100@1'}])),null);
assert.equal(observedControl({...feature(tags),sources:'invalid'}),null);
const a=[.125,.1875,origin.record_id];
assert.deepEqual(ownedControls([a],0,1),[]);
const owned=ownedControls([a,[.125,.18,'invalid']],1,1);
assert.deepEqual(owned,[{uv:[0,.5],osm:origin.record_id}]);
const grade=(properties)=>({type:2,extent:4096,properties,bbox:()=>[1024,1400,1024,1680],loadGeometry:()=>[[{x:1024,y:1400},{x:1024,y:1680}]]});
assert.deepEqual(surfaceControls(owned,{length:1,feature:()=>grade({brunnel:'bridge'})},1,1),[]);
assert.deepEqual(surfaceControls(owned,{length:1,feature:()=>grade({})},1,1),owned);
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'sd-signal-check-'));
try{
 let calls=0;const missing=async()=>{calls++;throw Error('offline')};
 assert.equal((await signalControls(10,10,dir,missing)).status,'unavailable');
 assert.equal((await signalControls(11,10,dir,missing)).status,'unavailable');
 assert.equal((await signalControls(-1,10,dir,missing)).status,'unsupported');
 assert.equal(calls,1);
}finally{fs.rmSync(dir,{recursive:true,force:true})}
console.log('Signal source, missing metadata, traffic control type, tile border, grade separations and shared network failure cooldown passed.');
