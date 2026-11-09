// Run after the ordinary atlas "tile" command has cached the area's base map.
import fs from 'node:fs';
import {footprintSurveyCandidates,localBuildingTile} from '../../helper/overture-buildings.mjs';

const [directory,inputX,inputY,output]=process.argv.slice(2);
const x=Number(inputX),y=Number(inputY);
if(!directory || !output || ![x,y].every(value=>Number.isInteger(value)&&value>=0&&value<65536)){
 throw Error('Expected CACHE X Y OUTPUT.json using zoom-16 slippy coordinates');
}
const baseline=localBuildingTile(x,y,directory);
const candidates=await footprintSurveyCandidates(x,y,directory);
fs.writeFileSync(output,JSON.stringify({...candidates,baseline:baseline.rows.map(row=>row[0]),fingerprint:baseline.fingerprint})+'\n');
console.log(baseline.rows.length+' existing footprints, '+candidates.candidates.length+' clearance-safe candidate outlines');
