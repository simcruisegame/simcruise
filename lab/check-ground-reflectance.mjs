// Offline crop from Copernicus Sentinel-2 L2A, 6 November 2026. See rangelands/README.md.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {bareReflectance} from '../helper/bare-reflectance.mjs';
for(const site of ['Utah','LaSal']) {
 const file=name=>fs.readFileSync(new URL('./rangelands/'+site+'-'+name+'.bin',import.meta.url));
 const rgba=file('sensor'),raw=file('bands'),classes=new Uint8Array(27*27).fill(5);
 const {output,measured,estimated}=bareReflectance(rgba,classes,27,27);
 let directError=0,redError=0;
 for(let i=0;i<classes.length;i++) {
  const alpha=output[i*4+3];
  for(let k=0;k<3;k++) {
   const physical=raw.readUInt16LE((i*3+k)*2)*.0001-.1;
   const error=Math.abs(output[i*4+k]/510-physical);
   if(alpha===255)directError+=error;
   if(alpha===224 && k===0)redError+=error;
  }
 }
 const mean=directError/(measured*3);
 assert.equal(measured,site==='Utah'?608:729);
 assert.equal(estimated,site==='Utah'?106:0);
 assert.ok(mean<.0021);
 if(estimated)assert.ok(redError/estimated<.014);
 console.log(site,{measured,estimated,mean,estimatedRed:estimated?redError/estimated:null});
 classes.fill(4);
 assert.ok(!bareReflectance(rgba,classes,27,27).output.some(Boolean));
}
const missing=Uint8Array.from([0,0,0,255,255,255,110,255]);
assert.ok(!bareReflectance(missing,Uint8Array.of(5,5),2,1).output.some(Boolean));
