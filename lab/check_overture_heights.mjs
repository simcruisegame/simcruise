// Run with: node lab/check_overture_heights.mjs
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {buildingEnrichment,matchedHeights,matchedFinishes} from '../helper/overture-buildings.mjs';

const ring=[[.1,.1],[.8,.1],[.8,.8],[.1,.8]],rect=[.1,.1,.8,.8];
const base=(height=5,min=0)=>[[ring],[0,1,2,0,2,3],height,min,.45,.45,'',''];
const attrs={osm_version:8,building:'yes',height:null,building_levels:null,levels:null,roof_height:null,roof_levels:null};
const props=(source='USGS Lidar',height=8.9)=>({id:'sample-gers-id',height,'@height_source':'OpenStreetMap',
 sources:JSON.stringify([{dataset:'OpenStreetMap',property:'',record_id:'w121@8'},...(source==='OpenStreetMap'?[]:[{dataset:source,property:'/properties/height'}])])});
const observed=(p=props())=>({rings:[ring],box:rect,props:p});
let calls=0;
const lookup=tags=>async()=>{calls++;return new Map([['121',tags]])};
const match=(r=[base()],o=[observed()],tags=attrs)=>matchedHeights(r,o,lookup(tags));
const good=await match();
assert.equal(good.length,1);assert.equal(good[0].heightM,8.9);assert.equal(good[0].baseIndex,0);assert.equal(good[0].source,'USGS Lidar');
assert.deepEqual(await match([base(),base()]),[]);
assert.deepEqual(await match([base()],[observed(),observed()]),[]);
assert.deepEqual(await match([base(12)]),[]);
assert.deepEqual(await match([base(5,1)]),[]);
assert.deepEqual(await match([base()],[observed(props('Microsoft ML Buildings'))]),[]);
assert.deepEqual(await match([base()],[observed({...props(),has_parts:true})]),[]);
assert.deepEqual(await match([base()],[observed({...props(),is_underground:true})]),[]);
for(const a of [{...attrs,height:5},{...attrs,building_levels:'1'},{...attrs,osm_version:9},{...attrs,height:undefined}]){
 assert.deepEqual(await match(undefined,undefined,a),[]);
}
const same=[observed(props('OpenStreetMap',4.4))];
assert.equal((await match([base()],same,{...attrs,height:4.4}))[0].heightM,4.4);
assert.deepEqual(await match([base()],same,{...attrs,height:5}),[]);
const cache=fs.mkdtempSync(path.join(os.tmpdir(),'sd-ov-test-'));
try{
 let fetches=0;const offline=()=>{fetches++;throw Error('offline')};
 const emptyBaseline={rows:[],stats:{requests:0,bytes:0}};
 const noBuildings=await buildingEnrichment(10,10,cache,offline,emptyBaseline);
 assert.equal(noBuildings.status,'empty');assert.equal(fetches,0);
 const noBase=await buildingEnrichment(11,11,cache,offline);
 assert.equal(noBase.status,'unavailable');assert.equal(noBase.requests,0);assert.equal(fetches,0);
 console.log('Height matching, ambiguous footprints, tagged heights, source version, source conflicts, unsupported geometry, empty and fail-open cases passed.');
}finally{fs.rmSync(cache,{force:true,recursive:true})}

const datedSource={dataset:'OpenStreetMap',record_id:'w9001@2',license:'ODbL-1.0',property:'',update_time:'2025-11-15T08:00:00Z'};
const surfaced=(details={},sources=[datedSource])=>observed({...props(),facade_color:'#FFCC99',roof_shape:'gabled',roof_height:4.2,roof_orientation:'across',sources:JSON.stringify(sources),...details});
const accepted=matchedFinishes([base()],[surfaced()]);
assert.equal(accepted.length,1);
assert.deepEqual(accepted[0].style,{wall_color:'#ffcc99',shape:'gabled',across:true,roof_height:4.2});
assert.equal(accepted[0].way,'w9001@2');
assert.deepEqual(matchedFinishes([base(),base()],[surfaced()]),[]);
assert.deepEqual(matchedFinishes([base()],[surfaced(),surfaced()]),[]);
assert.deepEqual(matchedFinishes([base()],[surfaced({has_parts:true})]),[]);
assert.deepEqual(matchedFinishes([base()],[surfaced({facade_color:'bad',roof_shape:'unknown'})]),[]);
assert.deepEqual(matchedFinishes([base()],[surfaced({},[{...datedSource,license:'unknown'}])]),[]);
const replaced=matchedFinishes([base()],[surfaced({},[datedSource,{property:'/properties/facade_color',dataset:'other'}])]);
assert.equal(replaced[0].style.wall_color,undefined);
console.log('Surface attributes, geometry matching, source priority and licensing checks passed.');

for(const alternate of [
 {...datedSource,record_id:'w7777@1'},
 {...datedSource,record_id:'w9001@1'},
 {...datedSource,license:undefined},
]){
 const foreign={...alternate,property:'/facade_color'};
 const [change]=matchedFinishes([base()],[surfaced({},[datedSource,foreign])]);
 assert.equal(change.style.wall_color,undefined);
 assert.equal(change.style.shape,'gabled');
}
for(const conflicting of [[datedSource,{...datedSource,record_id:'w3333@1'}],[{...datedSource,property:undefined}],[datedSource,null]]){
 assert.deepEqual(matchedFinishes([base()],[surfaced({},conflicting)]),[]);
}
for(const unsafe of [{has_parts:'true'},{is_underground:1}]){
 assert.deepEqual(matchedFinishes([base()],[surfaced(unsafe)]),[]);
}
// A dedicated finish on the same identified OSM way and revision is usable.
assert.equal(matchedFinishes([base()],[surfaced({},[datedSource,{...datedSource,property:'/facade_color'}])])[0].style.wall_color,'#ffcc99');

// Construct a one-building PMTiles v3 fixture with a surface and an independently
// sourced height candidate. This exercises the real archive reader, not just the join.
const {default:Pbf}=await import('pbf');
function fixtureArchive() {
 const properties={id:'test-001',height:8.9,facade_color:'#ffcc99',sources:JSON.stringify([datedSource,{property:'/height',dataset:'USGS Lidar'}])};
 const fieldNames=Object.keys(properties);
 const result=new Pbf();
 result.writeMessage(3,(_,layer)=>{
  layer.writeStringField(1,'building');layer.writeVarintField(5,4096);layer.writeVarintField(15,2);
  layer.writeMessage(2,(_,feature)=>{
   feature.writeVarintField(3,3);
   feature.writePackedVarint(2,fieldNames.flatMap((_,i)=>[i,i]));
   // Tile extent 4096, game sub-tile extent 1024. Square spans [128,768].
   feature.writePackedVarint(4,[9,256,256,26,1280,0,0,1280,1279,0,15]);
  });
  for(const key of fieldNames){
   layer.writeStringField(3,key);
   layer.writeMessage(4,(_,value)=>{
    if(typeof properties[key]==='number')value.writeDoubleField(3,properties[key]);
    else value.writeStringField(1,properties[key]);
   });
  }
 });
 const tile=Buffer.from(result.finish());
 const root=new Pbf();
 for(const value of [1,(4**14-1)/3,1,tile.length,1])root.writeVarint(value);
 const index=Buffer.from(root.finish());
 const header=Buffer.alloc(16384);
 header.write('PMTiles',0);header[7]=3;
 for(const [byte,v] of [[8,127],[16,index.length],[56,16384]])header.writeBigUInt64LE(BigInt(v),byte);
 header[97]=1;header[98]=1;header[99]=1;header[100]=14;header[101]=14;
 index.copy(header,127);
 return {header,tile};
}
const district=fs.mkdtempSync(path.join(os.tmpdir(),'sd-finish-review-'));
try{
 const {header,tile}=fixtureArchive();
 const version='2026-10-21.0';
 const catalogBytes=Buffer.from(JSON.stringify({latest:version}));
 fs.writeFileSync(path.join(district,'ov-catalog-v1'),catalogBytes);
 const r=[[.125,.125],[.75,.125],[.75,.75],[.125,.75]];
 const baseline={rows:[[[r],[0,1,2,0,2,3],5,0,.4375,.4375,'','']],stats:{bytes:0,requests:0},fingerprint:'fixture'};
 let archiveReads=0,tagCalls=0;
 baseline.read=async(key,url,ttl,range)=>{
  if(!range)return catalogBytes;
  archiveReads++;return range[0]===0?header:tile;
 };
 baseline.getBuildingTags=async()=>{tagCalls++;throw Error('independent replica offline')};
 const run=()=>buildingEnrichment(0,0,district,undefined,baseline);
 const first=await run();
 assert.equal(first.status,'unavailable');assert.equal(first.finishes[0].style.wall_color,'#ffcc99');
 assert.deepEqual(first.updates,[]);assert.equal(tagCalls,1);assert.equal(archiveReads,2);
 assert.equal((await run()).finishes[0].way,datedSource.record_id);
 assert.equal(tagCalls,1);assert.equal(archiveReads,2);
 assert.ok(!fs.existsSync(path.join(district,'ov-pause-0-0')));
 const partial=fs.readdirSync(district).find(file=>file.endsWith('.partial'));
 fs.utimesSync(path.join(district,partial),new Date(0),new Date(0));
 baseline.getBuildingTags=async()=>{tagCalls++;return new Map([['9001',{...attrs,osm_version:2}]])};
 const recovered=await run();
 assert.equal(recovered.status,'available');assert.equal(recovered.finishes.length,1);assert.equal(recovered.updates[0].heightM,8.9);
 assert.equal(tagCalls,2);
 const before=archiveReads;
 fs.writeFileSync(path.join(district,'ov-pause-0-0'),''); // unrelated sibling failure
 const warm=await run();assert.deepEqual(warm,recovered);assert.equal(archiveReads,before);assert.equal(tagCalls,2);
 console.log('Finish provenance and partial-height outage, warm reuse, recovery and independent sibling cooldown passed.');
}finally{fs.rmSync(district,{recursive:true,force:true})}
