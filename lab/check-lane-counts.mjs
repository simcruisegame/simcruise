// Offline test of helper/lane-counts.mjs on a synthetic ArcGIS answer: tag
// parsing, way decoding, and matching of tile road segments to mapped ways
// (direction, one-way pairs, majority vote). No network.
import assert from 'node:assert/strict';
import {count,mph,onewayOf,decodeLaneWays,matchLaneCounts,laneCountKey,laneCountQuery} from '../helper/lane-counts.mjs';

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
console.log('lane-counts: all assertions passed');
