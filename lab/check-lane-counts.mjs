// Offline test of helper/lane-counts.mjs on a synthetic ArcGIS answer: tag
// parsing, way decoding, and matching of tile road segments to mapped ways
// (direction, one-way pairs, majority vote). No network.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {roadWidth,LANE_M} from '../helper/road-widths.mjs';
import {count,mph,onewayOf,decodeLaneWays,matchLaneCounts,laneCountKey,laneCountQuery,cachedLaneWays,laneRoadsFile} from '../helper/lane-counts.mjs';

assert.equal(count('4'),4);assert.equal(count('2;3'),2);assert.equal(count(''),0);assert.equal(count('40'),0);assert.equal(count(null),0);
assert.equal(mph('35 mph'),35);assert.equal(mph('50'),31,'plain numbers are km/h');assert.equal(mph('fast'),0);
assert.equal(onewayOf('yes','primary'),1);assert.equal(onewayOf('-1','primary'),-1);assert.equal(onewayOf('','motorway'),1);assert.equal(onewayOf('','trunk'),0);
assert.equal(laneCountKey(101,203),laneCountKey(100,202),'2x2 tile groups share one request');
assert.ok(laneCountQuery(100,202).includes('lanes_forward'));

// One z16 tile at Y, in Web Mercator metres (EPSG:3857).
const X=14730,Y=24643,span=40075016.68557849/65536,cos=1/Math.cosh(Math.PI*(1-2*(Y+.5)/65536));
const west=X*span-span*32768,north=span*32768-Y*span;
// Tile-local metres (x east, y south) to 3857.
const m=(x,y)=>[west+x/cos,north-y/cos];
const side=span*cos;
const feature=(attrs,pts)=>({attributes:{osm_id2:1,highway:'primary',oneway:null,lanes:null,lanes_forward:null,lanes_backward:null,maxspeed:null,...attrs},geometry:{paths:[pts.map(p=>m(...p))]}});
const raw=JSON.stringify({features:[
 // Two-way, 5 lanes 2 forward (east) 3 backward, 30 mph, along y=100.
 feature({highway:'trunk',lanes:'5',lanes_forward:'2',lanes_backward:'3',maxspeed:'30 mph'},[[-50,100],[side+50,100]]),
 // A one-way pair 14 m apart along y=200 (east) and y=214 (west).
 feature({oneway:'yes',lanes:'2',maxspeed:'35 mph'},[[-50,200],[side+50,200]]),
 feature({oneway:'yes',lanes:'3'},[[side+50,214],[-50,214]]),
 // No useful tag: dropped.
 feature({highway:'tertiary'},[[0,300],[side,300]]),
]});
const ways=decodeLaneWays(raw);
assert.equal(ways.length,3,'ways without lane or speed tags are dropped');
assert.throws(()=>decodeLaneWays(JSON.stringify({exceededTransferLimit:true,features:[]})));
assert.throws(()=>decodeLaneWays(JSON.stringify({features:[{attributes:{highway:'footway'},geometry:{paths:[]}}]})));

const u=v=>v/side;
const roads=[
 ['trunk',0,'Main',[[0,u(101)],[1,u(101)]],16,'paved'],        // drawn east: forward 2, backward 3
 ['trunk',0,'Main',[[1,u(99)],[.5,u(99)]],16,'paved'],          // drawn west: counts swap
 ['primary',1,'A',[[0,u(201)],[1,u(201)]],12,'paved'],          // eastbound carriageway
 ['primary',1,'B',[[1,u(213)],[0,u(213)]],12,'paved'],          // westbound carriageway
 ['primary',0,'C',[[0,u(400)],[1,u(400)]],12,'paved'],          // nothing mapped near
 ['minor',0,'D',[[0,u(100)],[1,u(100)]],8,'paved'],             // local street: not matched
 ['trunk',0,'E',[[.5,0],[.5,1]],16,'paved'],                    // crosses at right angles: no match
];
const out=new Map(matchLaneCounts(X,Y,roads,ways));
assert.deepEqual(out.get(0),[[0,0,5,2,3,30]]);
assert.deepEqual(out.get(1),[[0,0,5,3,2,30]],'counts follow the tile road point order');
assert.deepEqual(out.get(2),[[0,0,2,2,0,35]],'one-way carriageway takes its own way, not the pair 12 m off');
assert.deepEqual(out.get(3),[[0,0,3,3,0,0]]);
assert.ok(!out.has(4)&&!out.has(5)&&!out.has(6));

// A one-way road takes a one-way way running its own way, never a nearer one
// running against it (the other carriageway, a side street's mouth), and
// never a two-way way.
const pair=decodeLaneWays(JSON.stringify({features:[
 feature({oneway:'yes',lanes:'3'},[[side+50,301],[-50,301]]),           // westbound, 1 m off
 feature({oneway:'yes',lanes:'2',maxspeed:'25 mph'},[[-50,305],[side+50,305]]),  // eastbound, 5 m off
 feature({lanes:'4'},[[-50,402],[side+50,402]]),                         // two-way, 2 m off road F
]}));
const onewayRoads=[
 ['primary',1,'East',[[0,u(300)],[1,u(300)]],12,'paved'],       // eastbound: the 5 m way
 ['primary',-1,'West',[[0,u(302)],[1,u(302)]],12,'paved'],      // drawn east, travels west: the 1 m way
 ['primary',1,'F',[[0,u(400)],[1,u(400)]],12,'paved'],          // one-way beside a two-way way: nothing
];
const dir=new Map(matchLaneCounts(X,Y,onewayRoads,pair));
assert.deepEqual(dir.get(0),[[0,0,2,2,0,25]],'one-way takes the way running its own direction');
assert.deepEqual(dir.get(1),[[0,0,3,3,0,0]],'oneway=-1 travels against the point order');
assert.ok(!dir.has(2),'a one-way road never takes a two-way way');

// The tile command reads the cache only: pending until the counts are fetched.
const tmp=fs.mkdtempSync(path.join(os.tmpdir(),'lane-counts-'));
try{
 assert.equal(cachedLaneWays(X,Y,tmp),null,'nothing cached: pending');
 fs.writeFileSync(path.join(tmp,laneCountKey(X,Y)),raw);
 const cached=cachedLaneWays(X,Y,tmp);
 assert.equal(cached.ways.length,3,'cached ways are read without a request');
 fs.unlinkSync(path.join(tmp,laneCountKey(X,Y)));
 fs.writeFileSync(path.join(tmp,laneCountKey(X,Y)+'.miss'),'0');
 assert.equal(cachedLaneWays(X,Y,tmp),'unavailable','a recent failed request is not retried at once');
 assert.ok(laneRoadsFile(tmp,X,Y).startsWith(tmp));
 assert.throws(()=>laneRoadsFile(tmp,-1,Y));
}finally{fs.rmSync(tmp,{recursive:true,force:true});}
// The game paints an unmapped one-way with the lanes its surface was built for
// (LaneMarkings.gd oneway_default_lanes: 2 when the paved width holds two
// 3.6 m lanes, else 1). Every one-way carriageway width must give 2 and every
// ramp width 1, or the paint would disagree with the surface.
{
 const gdLanes=w=>w>=2*LANE_M-.05?2:1;
 for(const cls of ['motorway','trunk','primary','secondary','tertiary']){
  assert.equal(gdLanes(roadWidth(cls,1,0)),2,cls+' one-way carriageway is painted with two lanes');
  assert.equal(gdLanes(roadWidth(cls,1,1)),1,cls+' ramp is painted with one lane');
 }
 const gd=fs.readFileSync(new URL('../game/LaneMarkings.gd',import.meta.url),'utf8');
 assert.match(gd,/const WIDTH_LANE_M := 3\.6\b/,'LaneMarkings.gd uses road-widths.mjs LANE_M');
 assert.match(gd,/const ONE_WAY_LANES := 2\b/,'LaneMarkings.gd uses road-widths.mjs ONE_WAY_LANES');
}
console.log('lane-counts: all assertions passed');
