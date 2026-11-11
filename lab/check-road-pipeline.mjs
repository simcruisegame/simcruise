// Offline checks for the tile pipeline gaps found by the 2026-11-10 field test:
// one-way carriageway widths (Vidalia GA, Eureka CA), roads that cross a tile
// with no vertex inside it (Leakey TX) and tiles without an elevation source
// (Bozeman MT). Synthetic inputs only; no network.
import assert from 'node:assert/strict';
import os from 'node:os';import fs from 'node:fs';import path from 'node:path';
import {createHash} from 'node:crypto';
import {roadWidth,roadWidths,referenceWidths,LANE_M} from '../helper/road-widths.mjs';
import {laneRoadSignature} from '../helper/road-layout.mjs';
import {crossesBox,chainVertices,chainPoint,FILLET_MAX_M} from '../helper/road-grade.mjs';
import {elevationGate,ELEVATION_TRIES,RETRY_WINDOW_MS} from '../helper/usgs-ground.mjs';
import {trimSourceCache} from '../helper/cache-eviction.mjs';

// Widths: two-way roads keep the class reference width; a one-way carriageway
// is two 3.6 m lanes plus an outside and a median shoulder; a ramp one lane.
assert.equal(LANE_M,3.6);
assert.deepEqual(referenceWidths,{motorway:19,trunk:16,primary:12,secondary:11,tertiary:10,minor:8,service:4.6,track:3});
assert.equal(roadWidth('trunk',0,0),16);
assert.equal(roadWidth('trunk',1,0),10.5);          // 2 x 3.6 + 2.4 + 0.9
assert.equal(roadWidth('trunk',-1,0),10.5);         // one-way against the drawing direction
assert.equal(roadWidth('motorway',1,0),11.4);       // 2 x 3.6 + 3 + 1.2
assert.equal(roadWidth('secondary',1,0),9.3);       // 2 x 3.6 + 1.5 + 0.6
assert.equal(roadWidth('tertiary',1,0),9);          // 2 x 3.6 + 1.2 + 0.6
assert.equal(roadWidth('motorway',1,1),6);          // a ramp: 1 lane, ramp shoulders
assert.equal(roadWidth('primary',0,1),6);           // ramps are one-way whatever the flag says
assert.equal(roadWidth('minor',1,0),8);             // classes without a carriageway rule keep their widths
assert.equal(roadWidth('service',0,0),4.6);
assert.equal(roadWidth('path',0,0),undefined);      // not drawn as a road
// Never wider than the reference width that bundled survey data is signed with.
for(const cls of Object.keys(referenceWidths))for(const one of [0,1])for(const ramp of [0,1])
 assert.ok(roadWidth(cls,one,ramp)<=referenceWidths[cls],cls+' '+one+' '+ramp);
// A function of the feature's properties alone (no tile, no network): both
// tiles of a seam give the same road the same width.
const feature=props=>({properties:props,loadGeometry:()=>{throw Error('geometry must not be read')}});
assert.equal(roadWidths.of(feature({class:'trunk',oneway:1})),10.5);
assert.equal(roadWidths.of(feature({class:'primary'})),12);
assert.equal(roadWidths.of(feature({class:'motorway',oneway:1,ramp:1})),6);
assert.equal(roadWidths.trunk,16);assert.equal(roadWidths.of.length,1);
assert.deepEqual(Object.keys(roadWidths),Object.keys(referenceWidths));

// The road signature signs the reference width of each road's class, so it is
// the main-branch signature (every road at its class width) and does not change
// when a road is narrower; geometry and class still change it.
const route=[[.1,.2],[.3,.4000004]];
const roads=[['trunk',1,'US 280',route,10.5],['tertiary',0,'Main',[[0,0],[1,1]],10],['minor',0,'',[[.5,0],[.5,1]],8]];
const legacy=rows=>createHash('sha256').update(rows.map(r=>Math.round(r[4]*100)+'|'+r[3].map(p=>Math.round(p[0]*1e6)+'/'+Math.round(p[1]*1e6)).join(',')).sort().join('\n')).digest('hex');
const atReference=roads.map(r=>[...r.slice(0,4),referenceWidths[r[0]]]);
assert.equal(laneRoadSignature(roads),legacy(atReference));
assert.equal(laneRoadSignature(roads),laneRoadSignature(atReference));
assert.notEqual(laneRoadSignature([['primary',...roads[0].slice(1)],...roads.slice(1)]),laneRoadSignature(roads));
assert.notEqual(laneRoadSignature([[...roads[0].slice(0,3),[[.1,.2],[.3,.400002]],10.5],...roads.slice(1)]),laneRoadSignature(roads));

const temp=fs.mkdtempSync(path.join(os.tmpdir(),'sd-pipeline-'));
try{
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

 // Cache eviction removes retry records a day old and manages the lane-width
 // files an earlier draft of this pipeline wrote.
 const state=path.join(temp,'.source-state'),now=Date.now();
 const stale=path.join(state,'elevation-retry-1-2.json'),fresh=path.join(state,'elevation-retry-3-4.json'),other=path.join(state,'banks-retry.until');
 for(const f of [stale,fresh,other])fs.writeFileSync(f,'[]');
 for(const f of [stale,other])fs.utimesSync(f,(now-2*86400000)/1000,(now-2*86400000)/1000);
 const lanes=path.join(temp,'lane-widths-v1-4400-6650-0');
 fs.writeFileSync(lanes,'x'.repeat(1000));fs.utimesSync(lanes,(now-3600000)/1000,(now-3600000)/1000);
 trimSourceCache(temp,0,null,now);
 assert.equal(fs.existsSync(stale),false);
 assert.equal(fs.existsSync(fresh),true);
 assert.equal(fs.existsSync(other),true);
 assert.equal(fs.existsSync(lanes),false);

 // The filleted centreline (road-grade.mjs chainVertices / chainPoint): no
 // corner at any mapped vertex, never more than FILLET_MAX_M from one, chain
 // ends exactly where they are mapped.
 const part=(pts,offset=0,reversed=false)=>{const s=[0];for(let k=1;k<pts.length;k++)s.push(s[k-1]+Math.hypot(pts[k][0]-pts[k-1][0],pts[k][1]-pts[k-1][1]));return {piece:{pts},s,length:s[s.length-1],reversed,offset}};
 const walkChain=(parts,step=.25)=>{
  const V=chainVertices(parts),total=parts.reduce((sum,p)=>sum+p.length,0),out=[];
  for(let st=0;st<=total+1e-9;st+=step)out.push(chainPoint(V,st));
  return {V,out};
 };
 // Largest heading jump between consecutive samples beyond the steady turn
 // of its neighbours (a corner), in degrees.
 const corner=out=>{let worst=0;const h=out.map(p=>Math.atan2(p[3],p[2]));const d=[];for(let k=1;k<h.length;k++)d.push(Math.abs(Math.atan2(Math.sin(h[k]-h[k-1]),Math.cos(h[k]-h[k-1]))));for(let k=1;k+1<d.length;k++)worst=Math.max(worst,d[k]-Math.max(d[k-1],d[k+1]));return worst*180/Math.PI};
 // A 30 m radius bend mapped every 10 m of arc: one smooth curve between its chords and its vertices.
 const R=30,arc=[];for(let a=0;a<=Math.PI/2+1e-9;a+=10/R)arc.push([R*Math.cos(a),R*Math.sin(a)]);
 const bend=walkChain([part(arc)]);
 assert.ok(corner(bend.out)<.05,'bend corner '+corner(bend.out));
 const sagitta=R*(1-Math.cos(5/R));
 for(const p of bend.out){const r=Math.hypot(p[0],p[1]);assert.ok(r<=R+1e-6&&r>=R-sagitta-.05,'bend radius '+r)}
 assert.deepEqual(bend.out[0].slice(0,2),arc[0]);
 // A right-angle corner with 50 m legs: rounded, within FILLET_MAX_M of the mapped vertex.
 const elbow=walkChain([part([[0,0],[50,0],[50,50]])]);
 assert.ok(corner(elbow.out)<.05,'elbow corner '+corner(elbow.out));
 const nearest=Math.min(...elbow.out.map(p=>Math.hypot(p[0]-50,p[1])));
 assert.ok(nearest<=FILLET_MAX_M+1e-6&&nearest>FILLET_MAX_M*.9,'elbow distance '+nearest);
 // Two pieces sharing an end point (a reversed one among them) are one curve;
 // two that only meet within the chaining tolerance keep the joint as mapped.
 const shared=walkChain([part([[0,0],[20,0]]),part([[40,10],[20,0]],20,true)]);
 assert.ok(corner(shared.out)<.05,'shared joint corner '+corner(shared.out));
 const loose=chainVertices([part([[0,0],[20,0]]),part([[21,0],[40,10]],20)]);
 assert.ok(loose.every(v=>v.T===0),'a loose joint keeps its corner');
 console.log('road pipeline checks passed');
}finally{fs.rmSync(temp,{recursive:true,force:true})}
