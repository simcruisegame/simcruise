import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {anchorCrowns, cityInventory} from '../helper/civic-trees.mjs';

const crowns = [[.12,.22,10,7],[.23,.22,11,8],[.34,.22,12,9],[.45,.22,13,10],[.56,.22,14,11]];
const stem = (id,u,v,kind=0) => ({id, u, v, kind, dbh:.28, name:'test tree', provider:'fixture'});
const paired = crowns.map(([u,v],i) => stem(String(i),u + .003,v));
const orphan = stem('orphan',.6,.6);
const unique = anchorCrowns(crowns,[...paired,orphan],500);
assert.equal(unique.matched,5);
assert.equal(unique.estimated,1);
assert.equal(unique.trees.length,6);
assert.deepEqual(unique.trees[0],[.123,.22,10,7,.28,'test tree',0,.12,.22,'0','matched','fixture']);
assert.equal(crowns[0].length,4);
assert.deepEqual(unique.trees[5],[.6,.6,12,9,.28,'test tree',0,.6,.6,'orphan','peer estimate','fixture']);
assert.equal(anchorCrowns(crowns,[...paired.slice(0,4),orphan],500).estimated,0);
assert.equal(anchorCrowns(crowns,[...paired,{...orphan,kind:2}],500).estimated,0);
assert.equal(anchorCrowns(crowns,[...paired,{...orphan,dbh:.07}],500).estimated,0);
assert.equal(anchorCrowns(crowns,[...paired,stem('border',.002,.6)],500).estimated,0);
assert.equal(anchorCrowns(crowns,[...paired,stem('close',.125,.22)],500).matched,4);
assert.equal(anchorCrowns(crowns,[...paired,{...orphan,u:.123,v:.235}],500).estimated,0);
assert.equal(anchorCrowns(crowns,[...paired,orphan,stem('dupe',.601,.6)],500).estimated,0);
assert.equal(anchorCrowns(crowns,[],500).trees,crowns);
assert.equal(anchorCrowns([],paired,500).trees.length,0);

// An off-centre, independently surveyed stem may share a continuous canopy.
const offCenterPeak = [[.5,.5,12,11]];
const matureStem = stem('surveyed',.515,.5);
const continuous = new Uint8Array(512*512).fill(11);
assert.equal(anchorCrowns(offCenterPeak,[matureStem],500).matched,0);
assert.equal(anchorCrowns(offCenterPeak,[matureStem],500,continuous).matched,1);
const gap = continuous.slice();
gap[256*512+259]=0;
assert.equal(anchorCrowns(offCenterPeak,[matureStem],500,gap).matched,0);
assert.equal(anchorCrowns(offCenterPeak,[{...matureStem,dbh:.08}],500,continuous).matched,0);
assert.equal(anchorCrowns(offCenterPeak,[{...matureStem,u:.522}],500,continuous).matched,0);
assert.equal(anchorCrowns(offCenterPeak,[matureStem,stem('ambiguous',.5,.517)],500,continuous).matched,0);

const tmp = fs.mkdtempSync(path.join(os.tmpdir(),'sd-civic-trees-test-'));
try {
  let requests = 0;
  const count = () => requests++;
  const fakeInventory = async address => {
    const url = new URL(address);
    const [minX,minY,maxX,maxY] = url.searchParams.get('geometry').split(',').map(Number);
    const geometry = {x:(minX+maxX)/2,y:(minY+maxY)/2};
    const lincoln = url.hostname === 'gis.lincoln.ne.gov';
    const units = lincoln ? 'DBH' : 'diameter';
    assert.equal(url.searchParams.get('resultRecordCount'),'650');
    assert.match(url.searchParams.get('where'),lincoln ? /LifeCycle='Active'/ : /RemovalDate IS NULL/);
    const attributes = {TreeID:'m1',ScientificName:'Pinus strobus',CommonName:'Eastern white pine',[units]:16};
    return Response.json({features:[
      {geometry, attributes},
      {geometry:{x:minX-1,y:geometry.y}, attributes},
      {geometry, attributes:{...attributes,TreeID:'invalid site'}},
      {geometry, attributes:{...attributes,[units]:0}},
    ]});
  };
  for (const [x,y,provider] of [[15168,24617,'lincoln-ne'],[19823,24236,'cambridge-ma']]) {
    const start = requests;
    const city = await cityInventory(x,y,tmp,count,fakeInventory);
    assert.equal(city.stems.length,1);
    assert.deepEqual(city.stems[0],{provider,id:'m1',u:.5,v:.5,dbh:.4064,name:'Eastern white pine',species:'Pinus strobus',kind:2});
    assert.deepEqual((await cityInventory(x,y,tmp,count,fakeInventory)).stems,city.stems);
    assert.equal(requests-start,1);
  }
  const broken = () => Response.json({features:[],exceededTransferLimit:true});
  assert.equal((await cityInventory(15167,24617,tmp,count,broken)).source,null);
  assert.equal((await cityInventory(15167,24617,tmp,count,broken)).source,null);
  assert.equal((await cityInventory(15166,24617,tmp,count,() => new Response('large',{headers:{'Content-Length':1e7}}))).source,null);
  assert.equal((await cityInventory(15165,24617,tmp,count,() => new Response(new Uint8Array(450*1024)))).source,null);
  assert.equal((await cityInventory(15168,24618,tmp,count,() => new Response('unavailable',{status:503}))).source,null);
  assert.equal((await cityInventory(18166,27914,tmp,count,fakeInventory)).source,null);
  assert.equal((await cityInventory(NaN,24617,tmp,count,fakeInventory)).source,null);
  assert.equal(requests,6);
} finally {
  fs.rmSync(tmp,{recursive:true,force:true});
}
console.log('Civic tree tests passed: unique matching, local sizing, duplicates, extent, units, byte bounds, misses and cache.');
