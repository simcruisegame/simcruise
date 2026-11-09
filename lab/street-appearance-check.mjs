// bun lab/street-appearance-check.mjs [street-sample-output.json ...]
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {estimateStreetAppearance} from '../helper/street-appearance.mjs';

const frames = Array.from({length:3},(_,i) => ({
  id:String(321+i),sequenceId:'fixture',sequenceIndex:10+i,lat:38,lon:-78+i*.00021,heading:90,cameraType:'PLANE'
}));
function photographedScene(change) {
  const width=176,height=112,data=new Uint8Array(width*height*3);
  for(let y=0;y<height;y++)for(let x=0;x<width;x++){
    const u=x/width,v=y/height;
    const variation=((x*11+y*17+x*y*7)%37)-18;
    let color=[112+variation*.35,109+variation*.35,105+variation*.35];
    if (v<.44) color=[102,157,205];
    if ((u<.29||u>.71)&&v>.28&&v<.59) color=[61+variation,84+variation,44+variation];
    if (change==='night') color=color.map(c=>c*.13);
    if (change==='glare' && v>.55 && v<.80) color=[210,218,232];
    if (change==='one-sided' && v>.54 && u<.50) color=[186,161,60];
    if (change==='rotated') color=v<.54?[55,55,52]:[102,157,205];
    if (change==='green-wall' && (u<.29||u>.71)&&v>.28&&v<.59) color=[50,140,35];
    data.set(color,(y*width+x)*3);
  }
  return {width,height,channels:3,data};
}
async function checkFrames(change,settings={}) {
  const pixels=photographedScene(change);
  return estimateStreetAppearance(frames.map(f=>({...f,pixels,...settings})));
}
const daytime=await checkFrames();
assert.equal(daytime.status,'accepted');
assert.ok(daytime.vegetation && daytime.road && daytime.road.samples===3);
assert.equal((await checkFrames('green-wall')).vegetation,null);
assert.equal((await checkFrames('glare')).reason,'possible_wet_or_glare');
for(const variant of ['night','one-sided','rotated'])
  assert.equal((await checkFrames(variant)).status,'rejected');
assert.equal((await checkFrames(null,{heading:null})).reason,'missing_pose');
assert.equal((await checkFrames(null,{heading:0})).reason,'view_route_disagreement');
assert.equal((await checkFrames(null,{cameraType:'spherical'})).reason,'unsupported_camera');
assert.equal((await checkFrames(null,{cameraType:null})).reason,'unsupported_camera');
assert.equal((await checkFrames(null,{id:'1'})).status,'rejected');
assert.equal((await estimateStreetAppearance([])).status,'insufficient');
console.log('Appearance checks passed for material estimates, illumination, occlusion, orientation, route geometry, and empty coverage.');
for(const file of process.argv.slice(2)){
  const input=JSON.parse(await fs.readFile(file,'utf8'));
  const result=await estimateStreetAppearance((input.frames??input).slice(0,3));
  console.log(JSON.stringify({file,result}));
}
