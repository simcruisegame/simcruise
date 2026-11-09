import assert from 'node:assert/strict';
import {conditions} from '../../helper/conditions.mjs';
const time=Math.floor(Date.now()/1000);
const row={icaoId:'KJAC',lat:43.608,lon:-110.737,obsTime:time,visib:'10+',cover:'CLR',wxString:null};
async function at(override,latitude=43.754) {
 let count=0;
 const result=await conditions(latitude,-110.624,async (key,url,age)=>{
  count++;
  assert.equal(age,900000);
  assert.equal(new URL(url).hostname,'aviationweather.gov');
  return Buffer.from(JSON.stringify([{...row,...override}]));
 });
 assert.equal(count,1);
 return result;
}
assert.equal((await at({})).clear_sky,true);
assert.equal((await at({cover:'SKC'})).clear_sky,true);
for(const override of [{cover:'NSC'},{cover:'FEW'},{cover:'OVC'},{wxString:'BR'},{wxString:'-RA'},{visib:2},{visib:null}])assert.equal((await at(override)).clear_sky,false);
assert.equal((await at({},44.08)).clear_sky,false);
assert.deepEqual(await at({obsTime:time-18000}),{});
console.log('Existing NOAA METAR read, TTL, distance, stale, visibility, cloud and wet-weather checks passed.');
