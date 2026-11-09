import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import records from './street-probes.json' with {type:'json'};
import independent from './audit-nodes.json' with {type:'json'};
import {cacheReader} from '../../../helper/atlas-cache.mjs';
import {surveyPatch,decodePatch} from '../../../helper/roof-survey.mjs';
import {fitRoofPatch} from '../../../helper/roof-fit.mjs';

const [work,...sites]=process.argv.slice(2);
if(!work||!sites.length)throw Error('Usage: bun replay.mjs WORK SITE [SITE ...]');
const root=path.resolve(import.meta.dir,'../../..'),cache=path.join(work,'cache');
fs.mkdirSync(cache,{recursive:true});
const read=cacheReader(cache,()=>{});
const write=(name,content)=>fs.writeFileSync(path.join(work,name),content);
for(const site of sites){
 const recording=records.find(r=>r.site===site);
 if(!recording)throw Error('Unknown study site '+site);
 const [lat,lon]=recording.point,tx=(lon+180)/360*65536,ty=(1-Math.asinh(Math.tan(lat*Math.PI/180))/Math.PI)*32768;
 const [x,y]=recording.tile,game=spawnSync(process.execPath,[path.join(root,'helper','atlas.mjs'),'tile',cache,String(x),String(y)],{encoding:'utf8'});
 if(game.status!==0)throw Error(game.stderr);
 const base=JSON.parse(game.stdout),g=base.grid;
 write(site+'-base.json',game.stdout);
 const ground=base.h[Math.round((ty-y)*g)*(g+1)+Math.round((tx-x)*g)];
 const study=await surveyPatch(x,y,Math.floor((tx-x)*4),Math.floor((ty-y)*4),ground,base.buildings,read);
 if(study.project?.name!==recording.project?.name)throw Error('Survey has changed at '+site);
 if(!study.points.length){console.log(site,study.status);continue;}
 const used=new Set(study.nodes.map(n=>n.key));
 const keys=independent[site]??[];
 if(keys.some(k=>used.has(k)))throw Error('Audit node overlaps a fitted node');
 const profiles=fitRoofPatch(study),earth=40075016.68557849;
 write(site+'-probe.json',JSON.stringify({side:study.side,ground:study.ground,nodes:study.nodes,project:study.project}));
 write(site+'-fit.json',JSON.stringify(profiles));
 const chunks=[];
 for(const key of keys){
  const name=study.project.name;
  const bytes=await read('roof-laz-'+name+'-'+key,'https://usgs-lidar-public.s3.amazonaws.com/'+name+'/ept-data/'+key+'.laz',180*86400000,null,9000,2400000);
  const cloud=await decodePatch(bytes,x*earth/65536-earth/2,earth/2-y*earth/65536,study.side*65536/earth);
  chunks.push(Buffer.from(cloud.buffer,cloud.byteOffset,cloud.byteLength));
 }
 write(site+'-audit.f32',Buffer.concat(chunks));
 console.log(site,profiles.profiles.length,'roofs',keys.length,'independent nodes');
}
