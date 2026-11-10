// Offline checks for the tile pipeline gaps found by the 2026-11-10 field test:
// road widths from OSM lanes (Vidalia GA, Eureka CA), roads that cross a tile
// with no vertex inside it (Leakey TX) and tiles without an elevation source
// (Bozeman MT). Synthetic inputs only; no network.
import assert from 'node:assert/strict';
import os from 'node:os';import fs from 'node:fs';import path from 'node:path';
import {laneWidth,laneWidths,classWidths,LANE_M} from '../helper/lane-widths.mjs';
import {crossesBox} from '../helper/road-grade.mjs';
import {elevationGate,ELEVATION_TRIES,RETRY_WINDOW_MS} from '../helper/usgs-ground.mjs';

// Widths: lanes x 3.6 m plus shoulders by class; one-way carriageways get an
// outside and a median shoulder.
assert.equal(LANE_M,3.6);
assert.equal(laneWidth('trunk',0,0),12);            // 2 x 3.6 + 2 x 2.4
assert.equal(laneWidth('trunk',1,0),10.5);          // 2 x 3.6 + 2.4 + 0.9
assert.equal(laneWidth('trunk',0,0,4),19.2);        // 4 x 3.6 + 2 x 2.4
assert.equal(laneWidth('trunk',1,0,3),14.1);        // 3 x 3.6 + 2.4 + 0.9
assert.equal(laneWidth('secondary',1,0,1),5.7);     // 3.6 + 1.5 + 0.6
assert.equal(laneWidth('primary',0,0,1),10.8);      // a two-way road has at least 2 lanes
assert.equal(laneWidth('primary',1,1),6);           // a ramp: 1 lane, ramp shoulders
assert.equal(laneWidth('tertiary',0,0,null,7),9.4); // OSM width= is the carriageway
assert.equal(laneWidth('minor',1,0,3),8);           // classes without lane data keep their widths
assert.equal(classWidths.trunk,12);assert.equal(classWidths.service,4.6);
for(const cls of ['motorway','trunk','primary','secondary','tertiary'])assert.ok(laneWidth(cls,1,0)<laneWidth(cls,0,0)+1e-9,cls);

// Matching a vector-tile road to the OSM way under it.
const EARTH=40075016.68557849,HALF=EARTH/2,SPAN=EARTH/16384,px=4400,py=6650;
const feature=(props,pts)=>({properties:props,extent:4096,loadGeometry:()=>[pts.map(([x,y])=>({x,y}))],
 bbox:()=>[Math.min(...pts.map(p=>p[0])),Math.min(...pts.map(p=>p[1])),Math.max(...pts.map(p=>p[0])),Math.max(...pts.map(p=>p[1]))]});
const mercator=([x,y])=>[px*SPAN-HALF+x*SPAN/4096,HALF-py*SPAN-y*SPAN/4096];
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'sd-lanes-'));
try{
 const ways=[
  {attributes:{highway:'trunk',oneway:'yes',lanes:'3',width:null},geometry:{paths:[[[0,1000],[4096,1000]].map(mercator)]}},
  {attributes:{highway:'trunk',oneway:'yes',lanes:null,width:null},geometry:{paths:[[[4096,1200],[0,1200]].map(mercator)]}},
  {attributes:{highway:'secondary',oneway:null,lanes:'4',width:null},geometry:{paths:[[[2000,0],[2000,4096]].map(mercator)]}},
 ];
 let reads=0;
 const read=async()=>{reads++;return Buffer.from(JSON.stringify({features:ways,exceededTransferLimit:false}))};
 const widths=laneWidths(temp,read);
 assert.equal(await widths.load(px,py),true);
 assert.equal(widths.of(feature({class:'trunk',oneway:1},[[0,1000],[4096,1000]]),px,py),14.1);
 // The other carriageway, 2 lanes by default, keeps its one-way width.
 assert.equal(widths.of(feature({class:'trunk',oneway:1},[[4096,1201],[0,1201]]),px,py),10.5);
 // Direction must agree: a cross street over a matched way does not take its lanes.
 assert.equal(widths.of(feature({class:'secondary'},[[2000,100],[2000,3000]]),px,py),17.4);
 assert.equal(widths.of(feature({class:'secondary'},[[100,2000],[3000,2000]]),px,py),classWidths.secondary);
 assert.equal(widths.of(feature({class:'minor'},[[0,0],[10,10]]),px,py),8);
 assert.equal(reads,1);
 // An unreadable source falls back to the class table and is not retried at once.
 const broken=laneWidths(temp,async()=>{throw Error('offline')});
 assert.equal(await broken.load(px+1,py),false);
 assert.equal(broken.of(feature({class:'trunk',oneway:1},[[0,1000],[4096,1000]]),px+1,py),10.5);

 // A segment that crosses a box with both vertices outside it is near the box.
 assert.equal(crossesBox([[-100,50],[200,50]],0,0,100,100),true);
 assert.equal(crossesBox([[-100,-100],[200,200]],0,0,100,100),true);
 assert.equal(crossesBox([[-100,150],[200,150]],0,0,100,100),false);
 assert.equal(crossesBox([[50,50]],0,0,100,100),true);

 // A transient elevation failure rejects the tile ELEVATION_TRIES times, then
 // builds it on Terrarium; a success clears the record; lasting failures (null) pass.
 const failed={unavailable:true,reason:'timeout'},t0=1e12;
 for(let k=0;k<ELEVATION_TRIES;k++)assert.throws(()=>elevationGate(temp,10,20,failed,t0+k*15000),/retry the tile/);
 assert.equal(elevationGate(temp,10,20,failed,t0+60000),null);
 const ok={source:'USGS 3DEP 1/3 arc-second'};
 assert.equal(elevationGate(temp,10,20,ok,t0+75000),ok);
 assert.throws(()=>elevationGate(temp,10,20,failed,t0+90000),/retry the tile/);
 assert.equal(elevationGate(temp,10,21,null,t0),null);
 // Old failures leave the window.
 for(let k=0;k<ELEVATION_TRIES;k++)assert.throws(()=>elevationGate(temp,11,20,failed,t0+k));
 assert.throws(()=>elevationGate(temp,11,20,failed,t0+RETRY_WINDOW_MS+10));
 console.log('road pipeline checks passed');
}finally{fs.rmSync(temp,{recursive:true,force:true})}
