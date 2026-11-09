import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fetchObjects} from '../helper/street-objects.mjs';
const cache=fs.mkdtempSync(path.join(os.tmpdir(),'street-details-'));
try {
 const x=18166, y=27914;
 const lon=(x+.5)/65536*360-180;
 const lat=Math.atan(Math.sinh(Math.PI*(1-2*(y+.5)/65536)))*180/Math.PI;
 const sample=JSON.stringify({elements:[{type:'node',id:7,lon,lat,tags:{highway:'traffic_signals'}}]});
 let calls=0;
 const read=async(_id,url)=>{calls++;assert.equal(new URL(url).hostname,'maps.example.test');return Buffer.from(sample)};
 assert.deepEqual((await fetchObjects(x,y,cache,read,'')).signals,[]);
 assert.equal(calls,0);
 fs.writeFileSync(path.join(cache,'street-osm-v2-'+(x>>2)+'-'+(y>>2)),sample);
 assert.equal((await fetchObjects(x,y,cache,read,'')).signals.length,1);
 assert.equal(calls,0);
 assert.equal((await fetchObjects(x,y,cache,read,'https://maps.example.test/interpreter')).signals.length,1);
 assert.equal(calls,1);
 console.log('PASS zero default Overpass calls, cached observation and configured interpreter');
} finally {fs.rmSync(cache,{recursive:true,force:true})}
