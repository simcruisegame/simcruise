// After an ordinary tile load: bun lab/lidar/footprint_candidates.mjs CACHE X Y OUT.json
import fs from 'node:fs';
import {footprintSurveyCandidates} from '../../helper/overture-buildings.mjs';
const [cache,sx,sy,destination]=process.argv.slice(2);
const x=Number(sx),y=Number(sy);
if(!cache||!destination||![x,y].every(n=>Number.isInteger(n)&&n>=0&&n<65536))
 throw Error('Expected CACHE X Y OUT.json for a zoom-16 game tile');
const result=await footprintSurveyCandidates(x,y,cache);
fs.writeFileSync(destination,JSON.stringify(result,null,2)+'\n');
console.log(result.candidates.length+' separated outlines from release '+result.release);
