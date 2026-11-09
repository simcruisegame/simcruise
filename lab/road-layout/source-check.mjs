import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {cachedLaneLayout, fetchLaneDistrict} from '../../helper/road-layout.mjs';
import {cacheReader} from '../../helper/atlas-cache.mjs';

const x = 10434, y = 23439;
const tileSpan = 40075016.68557849/65536;
const west = tileSpan*(x-32768), north = tileSpan*(32768-y);
const cos = 1/Math.cosh(Math.PI*(1-2*(y+.5)/65536));
const side = tileSpan*cos;
const a = [.1,.5], b = [.9,.5];
const roads = [['tertiary',1,'Example Street',[a,b],10,'paved']];
const project = p => [west+p[0]*tileSpan,north-p[1]*tileSpan];
function feature(change={},shift=0) {
 return {
  attributes:{osm_id2:'1234567',highway:'tertiary',name:'Example Street',oneway:'yes',lanes:'1',...change},
  geometry:{paths:[[a,b].map(p=>project([p[0],p[1]+shift/side]))]}
 };
}
const directory=fs.mkdtempSync(path.join(os.tmpdir(),'sunday-driver-layout-check-'));
try{
 let calls=0,fail=false,features=[feature()];
 const read=cacheReader(directory,()=>calls++,async url=>{
  assert.equal(new URL(url).hostname,'services6.arcgis.com');
  if(fail)throw Error('Simulated outage');
  return new Response(JSON.stringify({features}),{status:200});
 });
 const file=path.join(directory,'lane-layout-v1-10434-23438');
 assert.equal(cachedLaneLayout(x,y,roads,directory),null);
 const first=await fetchLaneDistrict(x,y,directory,read);
 assert.equal(first.status,'checked');assert.equal(calls,1);
 await fetchLaneDistrict(x+1,y,directory,read);
 assert.equal(calls,1,'Neighbor shares the same raw district');
 const saved=()=>cachedLaneLayout(x,y,roads,directory);
 assert.ok(saved().spans.length);
 assert.ok(saved().spans.every(s=>s.way==='1234567'&&s.road===0));
 function replace(rows){fs.writeFileSync(file,JSON.stringify({features:rows}));}
 for(const tags of [{lanes:null},{lanes:'2'},{bridge:'yes'},{tunnel:'yes'},{junction:'roundabout'},{layer:'1'}]){
  replace([feature(tags)]);assert.equal(saved().spans.length,0);
 }
 replace([feature({},3)]);assert.equal(saved().spans.length,0,'Parallel displaced way');
 replace([feature(),feature({lanes:'2',osm_id2:'7654321'})]);assert.equal(saved().spans.length,0,'Ambiguous stacked paths');
 replace([feature()]);assert.equal(cachedLaneLayout(x,y,[...roads,['service',0,'Crossing',[[.5,.1],[.5,.9]],4]],directory).spans.some(s=>s.a[0]<.5 && s.b[0]>.5),false);
 replace([]);assert.equal(saved().status,'checked');assert.equal(saved().spans.length,0);
 fs.writeFileSync(file,JSON.stringify({features:[],exceededTransferLimit:true}));
 assert.equal(saved(),null,'A clipped source cannot establish coverage');
 assert.equal((await fetchLaneDistrict(x,y,directory,read)).status,'unavailable');
 assert.equal(fs.existsSync(file),false,'A partial HTTP 200 ages as a miss, never as checked coverage');
 fs.rmSync(file+'.miss');fail=true;
 assert.equal((await fetchLaneDistrict(x,y,directory,read)).status,'unavailable');
 const during=calls;await fetchLaneDistrict(x,y,directory,read);assert.equal(calls,during,'Shares six-hour cooldown');
 console.log('Passed unknown, empty, explicit lane count, bridge, tunnel, level, junction, ambiguity, displaced road, warm group, outage and cooldown.');
}finally{fs.rmSync(directory,{recursive:true,force:true});}
