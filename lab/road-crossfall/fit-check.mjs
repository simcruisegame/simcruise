import assert from 'node:assert/strict';
import {mkdtempSync,rmSync,writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {measuredSections,roadControls} from '../../helper/road-crossfall.mjs';
import registered from '../../helper/crossfall-roads.json' with {type:'json'};

const controls={sections:[0,1,2,3].map(i=>[0,'101',12+6*i,50+6*i,60,1,0,3.2])};
function measured(secondSlope,mode='two'){
 const data=[],metadata=[];
 for(const source of [42,83]){
  if(mode==='one'&&source===83)continue;
  for(let x=42;x<=76;x+=.7)for(let u=-3;u<=3;u+=.55){
   const z=340+x*.03-.012*Math.abs(u)+u*(source===42?.02:secondSlope);
   data.push(x,60+u,z,mode==='roof'?6:2);
   metadata.push((mode==='zero'?0:source)<<16|1<<8|1);
  }
 }
 const study={side:160,points:[new Float32Array(data)],metadata:[new Uint32Array(metadata)],project:{name:'example'}};
 return measuredSections(study,controls,[1,1]);
}
const ok=measured(.024);
assert.equal(ok.length,1);assert.equal(ok[0].length,4);
for(const row of ok[0]){assert.ok(Math.abs(row[9]-.032)<.0005);assert.ok(Math.abs(row[10]-.008)<.0005);assert.equal(row[11],42);assert.equal(row[12],83);}
for(const mode of ['one','zero','roof'])assert.equal(measured(.024,mode).length,0);
assert.equal(measured(.06).length,0);
const dir=mkdtempSync(path.join(tmpdir(),'road-control-check-'));
try{
 const [key,snapshot]=Object.entries(registered)[0];
 const [x,y]=key.split('-').map(Number);
 // Test against a replay prepared today, even after the distributed register ages out.
 const override=path.join(dir,'road-controls-'+key+'.json');
 writeFileSync(override,JSON.stringify({...snapshot,prepared:new Date().toISOString().slice(0,10)}));
 assert.equal(roadControls(x,y,snapshot.mapFingerprint,dir)?.roadSignature,snapshot.roadSignature);
 assert.equal(roadControls(x,y,'000000000000000000000000',dir),null);
 assert.equal(roadControls(0,0,snapshot.mapFingerprint,dir),null);
 writeFileSync(override,JSON.stringify({...snapshot,prepared:'2000-01-01'}));assert.equal(roadControls(x,y,snapshot.mapFingerprint,dir),null);
}finally{rmSync(dir,{recursive:true,force:true});}
console.log('Paired strips recover both slopes; single, missing IDs, non-ground, incompatible slopes and unregistered vector tiles abstain.');
