// Optional background building-height enrichment. The ordinary road/terrain tile never waits for it.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {archiveTile} from './overture-archive.mjs';
import {classifyRings} from '@mapbox/vector-tile';
import {vectorTile} from './vector-tile.mjs';
import {verifiedFootprints,hasFootprintSurvey,footprintCandidates,footprintSurveyVersion} from './verified-footprints.mjs';
import earcut from 'earcut';
import {cacheReader} from './atlas-cache.mjs';

const day=86400000, totalBudget=2*1024*1024, maxCalls=7;
export const buildingFingerprint=b=>createHash('sha256').update(b).digest('hex').slice(0,24);
const rounded=n=>Math.round(n*100000)/100000;
const attribution='Overture Maps Foundation and contributors • OpenStreetMap contributors • ODbL 1.0';
const catalog='https://stac.overturemaps.org/catalog.json';
const replica='https://services6.arcgis.com/Do88DoK2xjTUCXd1/arcgis/rest/services/OSM_NA_Buildings/FeatureServer/0/query';

// Shared with the foreground tile builder. The index and rounded center identify the exact rendered polygon.
export function buildingRows(layer,x,y) {
 const rows=[];
 if(!layer)return rows;
 for(let i=0;i<layer.length;i++) {
  const f=layer.feature(i),box=f.bbox(),size=f.extent/4,sx=(x%4)*size,sy=(y%4)*size;
  if(f.type!==3||f.properties.hide_3d||box[2]<sx||box[0]>sx+size||box[3]<sy||box[1]>sy+size)continue;
  for(const poly of classifyRings(f.loadGeometry())) {
   const rings=poly.map(r=>r.slice(0,-1).map(p=>[rounded((p.x-sx)/size),rounded((p.y-sy)/size)])).filter(r=>r.length>=3);
   if(!rings.length)continue;
   const r=rings[0],c=r.reduce((s,p)=>[s[0]+p[0]/r.length,s[1]+p[1]/r.length],[0,0]);
   if(c.some(v=>v<0||v>=1))continue;
   const holes=[];let count=r.length;
   for(const hole of rings.slice(1)){holes.push(count);count+=hole.length}
   const p=f.properties;
   rows.push([rings,earcut(rings.flat(2),holes,2),p.render_height||5,p.render_min_height||0,rounded(c[0]),rounded(c[1]),String(p.class||''),String(p.subclass||'')]);
  }
 }
 return rows;
}

function bounds(ring){
 const b=[Infinity,Infinity,-Infinity,-Infinity];
 for(const [x,y] of ring){b[0]=Math.min(b[0],x);b[1]=Math.min(b[1],y);b[2]=Math.max(b[2],x);b[3]=Math.max(b[3],y)}
 return b;
}
function intersects(a,b){return a[0]<b[2]&&b[0]<a[2]&&a[1]<b[3]&&b[1]<a[3]}
function inside(rings,x,y){
 let odd=false;
 for(const ring of rings)for(let i=0,j=ring.length-1;i<ring.length;j=i++){
  const a=ring[i],b=ring[j];if((a[1]>y)!==(b[1]>y)&&x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0])odd=!odd;
 }
 return odd;
}
function agreement(a,b) {
 const A=a.box,B=b.box;
 const left=Math.min(A[0],B[0]),top=Math.min(A[1],B[1]),dx=(Math.max(A[2],B[2])-left)/32,dy=(Math.max(A[3],B[3])-top)/32;
 let union=0,both=0;
 for(let j=0;j<32;j++)for(let i=0;i<32;i++){
  const x=left+dx*(i+.5),y=top+dy*(j+.5),u=inside(a.rings,x,y),v=inside(b.rings,x,y);
  if(u||v)union++;if(u&&v)both++;
 }
 return union?both/union:0;
}
function records(layer,x,y) {
 const result=[],sx=x%4,sy=y%4;if(!layer)return result;
 if(layer.length>15000)throw Error('Building layer outside feature budget');
 for(let i=0;i<layer.length;i++){
  const f=layer.feature(i),box=f.bbox(),q=f.extent/4;
  if(f.type!==3||box[2]<sx*q||box[0]>(sx+1)*q||box[3]<sy*q||box[1]>(sy+1)*q)continue;
  for(const poly of classifyRings(f.loadGeometry())){
   if(result.length>2000)throw Error('District outside matching budget');
   const rings=poly.map(r=>r.slice(0,-1).map(p=>[p.x/q-sx,p.y/q-sy]));
   if(rings[0]?.length<3)continue;
   result.push({rings,box:bounds(rings[0]),props:f.properties});
  }
 }
 return result;
}
function provenance(props) {
 let rows;try{rows=JSON.parse(props.sources||'[]')}catch{return null}
 if(!Array.isArray(rows)||rows.some(r=>!r||typeof r!=='object'))return null;
 const particular=rows.find(r=>r.property==='/properties/height'||r.property==='/height');
 const osm=rows.find(r=>r.dataset==='OpenStreetMap'&&(!r.property||r.property==='/geometry'));
 const way=/^w(\d+)@(\d+)$/.exec(osm?.record_id||'');
 const source=particular??(props['@height_source']==='OpenStreetMap'?osm:null);
 if(!source||!way)return null;
 return {source:source.dataset,sourceRecordUpdate:particular?.update_time??null,osmId:way[1],osmVersion:Number(way[2]),propertySpecific:!!particular,
  license:source.license??null};
}
// Current upstream tags disambiguate the OpenMapTiles default 5 m from an explicit 5 m building.
// A newer revision, a deleted way, a tagged height/levels, and a failed tag request all preserve the existing model.
function verified(tags,candidate) {
 if(!tags||tags.osm_version!==candidate.osmVersion||!tags.building||tags.building==='no')return false;
 if(candidate.precisionOnly)return Number.isFinite(tags.height)&&Math.abs(tags.height-candidate.update.heightM)<.025;
 return ['height','building_levels','levels','roof_height','roof_levels'].every(k=>Object.hasOwn(tags,k)&&tags[k]===null);
}
async function verifyDistrict(x,y,read) {
 const lon=n=>n/65536*360-180,lat=n=>Math.atan(Math.sinh(Math.PI*(1-2*n/65536)))*180/Math.PI;
 const bbox=[lon(x),lat(y+1),lon(x+1),lat(y)].map(n=>n.toFixed(7)).join(',');
 const params=new URLSearchParams({f:'json',where:'1=1',geometryType:'esriGeometryEnvelope',geometry:bbox,inSR:'4326',spatialRel:'esriSpatialRelIntersects',
  outFields:'osm_id,osm_id2,osm_version,building,height,building_levels,levels,roof_height,roof_levels,amenity,shop,office,tourism,usage,historic,man_made,religion,leisure,landuse',returnGeometry:'false',returnCentroid:'true',outSR:'4326',resultRecordCount:'512'});
 const data=JSON.parse((await read('ov-attrs-v3-'+x+'-'+y,replica+'?'+params,7*day,null,3000)).toString());
 if(!Array.isArray(data.features)||data.error||data.exceededTransferLimit||data.spatialReference?.wkid!==4326)throw Error('Missing read-only building tags');
 const ways=new Map(),ambiguous=new Set();
 for(const row of data.features){
  const a=row.attributes;
  if(!a||!Number.isSafeInteger(a.osm_id)||a.osm_id<=0||a.osm_id2!==String(a.osm_id))continue;
  const id=String(a.osm_id),center=row.centroid;
  const valid=center&&Number.isFinite(center.x)&&Number.isFinite(center.y)&&Math.abs(center.y)<85;
  a.center=valid?[(center.x+180)/360*65536-x,(1-Math.asinh(Math.tan(center.y*Math.PI/180))/Math.PI)*32768-y]:null;
  if(ways.has(id))ambiguous.add(id);
  ways.set(id,a);
 }
 for(const id of ambiguous)ways.delete(id);
 return ways;
}
function footprintMatches(baseRows,observed){
 if(baseRows.length>2000||observed.length>2000)throw Error('District outside matching budget');
 const baseline=baseRows.map((r,i)=>({i,r,rings:r[0],box:bounds(r[0][0])}));
 const eligible=[],seenA=new Uint16Array(baseline.length),seenB=new Uint16Array(observed.length);
 let samples=0;
 for(const a of baseline)for(const [j,b] of observed.entries()){
  if(!intersects(a.box,b.box))continue;
  if(a.rings.flat().length>300||b.rings.flat().length>300){
   // Any intersecting complex shape blocks an otherwise tempting match.
   seenA[a.i]++;seenB[j]++;continue;
  }
  if(++samples>3500)throw Error('District exceeds overlap budget');
  const score=agreement(a,b);
  if(score<.55)continue;
  seenA[a.i]++;seenB[j]++;
  if(score>=.87)eligible.push({a,b,j,score});
 }
 return eligible.filter(({a,b,j})=>seenA[a.i]===1 && seenB[j]===1 && a.r[3]===0 &&
   (b.props.is_underground==null||b.props.is_underground===false) && (b.props.has_parts==null||b.props.has_parts===false) && typeof b.props.id==='string');
}
async function heightUpdates(matches,verifyWays){
 const candidates=[];
 for(const {a,b,score} of matches) {
  const h=b.props.height,raw=a.r[2],p=provenance(b.props);
  if(!p||!Number.isFinite(h)||h<1||h>890||Math.abs(h-raw)<.045)continue;
  const sameRounding=p.source==='OpenStreetMap'&&Math.ceil(h-1e-7)===raw;
  const ambiguousDefault=raw===5&&p.propertySpecific&&['USGS Lidar','Esri Community Maps'].includes(p.source);
  if(!sameRounding&&!ambiguousDefault)continue;
  candidates.push({...p,precisionOnly:sameRounding,update:{baseIndex:a.i,center:[a.r[4],a.r[5]],baseHeightM:raw,heightM:+h.toFixed(2),source:p.source,
   sourceRecordUpdate:p.sourceRecordUpdate,featureId:b.props.id,basis:'reported',overlap:+score.toFixed(3),license:p.license,parts:[]}});
 }
 if(!candidates.length)return [];
 const ways=await verifyWays();
 return candidates.filter(c=>verified(ways.get(c.osmId),c)).map(c=>c.update).slice(0,192);
}
export async function matchedHeights(baseRows,observed,verifyWays){
 return heightUpdates(footprintMatches(baseRows,observed),verifyWays);
}
// Overture's display tiles can carry street-facing finishes alongside height.
// Only transfer values attached to an unambiguous OSM-derived footprint.
function surfaceUpdates(matches){
 const changes=[];
 for(const {a,b,score} of matches){
  const props=b.props;
  const hasFinish=['roof_shape','roof_color','roof_material','facade_material','facade_color'].some(k=>typeof props[k]==='string');
  if(!hasFinish)continue;
  let sources;try{sources=JSON.parse(props.sources)}catch{continue}
  if(!Array.isArray(sources)||sources.some(row=>!row||typeof row.property!=='string'))continue;
  const roots=sources.filter(row=>row.property==='');
  const osm=roots[0];
  if(roots.length!==1||osm.dataset!=='OpenStreetMap'||!/^w[0-9]+@[0-9]+$/.test(osm.record_id)||osm.license!=='ODbL-1.0')continue;
  const owned=key=>{
   const dedicated=sources.filter(row=>row.property==='/properties/'+key || row.property==='/'+key);
   if(!dedicated.length)return true;
   // The output names a single way revision and license. A dedicated source
   // may supersede the root even when it comes from the same OSM dataset.
   return dedicated.length===1 && dedicated[0].dataset===osm.dataset &&
    dedicated[0].record_id===osm.record_id && dedicated[0].license===osm.license;
  };
  const style={};
  for(const [input,output] of [['roof_color','roof_color'],['facade_color','wall_color'],['roof_material','roof_material'],['facade_material','wall_material'],['roof_shape','shape']]){
   const v=props[input];
   // A property-specific source supersedes the record-wide author. Other
   // inputs require their own provenance support before this game uses them.
   if(!owned(input))continue;
   if(typeof v!=='string')continue;
   if(output.endsWith('_color') && /^#([0-9a-f]{3}|[0-9a-f]{6})$/i.test(v))style[output]=v.toLowerCase();
   else if(output==='shape' && ['flat','gabled'].includes(v))style.shape=v;
   else if(output.endsWith('_material') && ['brick','bricks','wood','timber','weatherboard','clapboard','vinyl_siding','siding','metal','metal_sheet','copper','steel','corrugated_metal','glass','concrete','stone','sandstone','limestone','stucco','plaster','slate','asphalt_shingle','shingle','roof_tiles','tiles','tar_paper','bitumen','gravel'].includes(v))style[output]=v;
  }
  if(style.shape==='gabled'){
   if(owned('roof_orientation')&&props.roof_orientation==='across')style.across=true;
   if(owned('roof_height')&&Number.isFinite(props.roof_height)&&props.roof_height>0&&props.roof_height<=40)style.roof_height=props.roof_height;
  }
  if(!Object.keys(style).length)continue;
  changes.push({baseIndex:a.i,center:[a.r[4],a.r[5]],style,way:osm.record_id,source:'OpenStreetMap',license:osm.license,
   sourceRecordUpdate:osm.update_time??null,featureId:props.id,overlap:+score.toFixed(3)});
 }
 return changes.slice(0,192);
}
export function matchedFinishes(baseRows,observed){return surfaceUpdates(footprintMatches(baseRows,observed))}
function boundedReader(cache,stats,fetcher){
 const deadline=Date.now()+8500;
 return cacheReader(cache,()=>{},async(url,options)=>{
  const left=deadline-Date.now();
  if(left<=0||stats.requests>=maxCalls)throw Error('District request budget exceeded');
  stats.requests++;
  const response=await fetcher(url,{...options,redirect:'error',signal:AbortSignal.any([options.signal,AbortSignal.timeout(Math.max(1,left))])});
  if(!response.ok){await response.body?.cancel();return new Response(null,{status:response.status,headers:response.headers})}
  const range=options.headers.Range;
  const parsed=range&&/^bytes=(\d+)-(\d+)$/.exec(range);
  const max=parsed?Number(parsed[2])-Number(parsed[1])+1:url.startsWith(replica)?156*1024:96*1024;
  if((range&&(response.status!==206||!response.headers.get('content-range')?.startsWith(range.replace('=',' ')+'/')))||Number(response.headers.get('content-length'))>max){
   await response.body?.cancel();throw Error('Unexpected response length or range');
  }
  const reader=response.body.getReader(),chunks=[];let size=0;
  try{
   for(;;){const {done,value}=await reader.read();if(done)break;size+=value.length;stats.bytes+=value.length;
    if(size>max||stats.bytes>totalBudget){await reader.cancel();throw Error('District response outside byte budget')}chunks.push(value)}
  }finally{reader.releaseLock()}
  return new Response(Buffer.concat(chunks),{status:response.status,headers:response.headers});
 });
}
// Both optional layers key their updates to this exact already-downloaded OpenFreeMap vector.
export function localBuildingTile(x,y,cache,fetcher=fetch) {
 const mx=Math.floor(x/4),my=Math.floor(y/4);
 const raw=fs.readFileSync(path.join(cache,'v-'+mx+'-'+my));
 const vector=raw[0]===31?unpack(raw,2):raw;
 const layers=vectorTile(vector).layers,layer=layers.building,stats={requests:0,bytes:0};
 const read=boundedReader(cache,stats,fetcher);
 let tags;
 return {rows:buildingRows(layer,x,y),fingerprint:buildingFingerprint(vector),layers,layer,stats,read,getBuildingTags:()=>tags??=verifyDistrict(x,y,read)};
}
// Authoring entry point. Uses the same pinned-release reader, footprint and
// collision clearances as the runtime. Ordinary driving never invokes it.
export async function footprintSurveyCandidates(x,y,cache){
 const baseline=localBuildingTile(x,y,cache);
 const release=JSON.parse(await baseline.read('ov-catalog-v1',catalog,day,null,2200)).latest;
 if(!/^20\d\d-\d\d-\d\d\.\d{1,3}$/.test(release))throw Error('Invalid Overture release');
 const bytes=await archiveTile(x>>2,y>>2,release,baseline.read);
 const observed=bytes?records(vectorTile(bytes).layers.building,x,y):[];
 return {tile:[x,y],release,candidates:footprintCandidates(x,y,baseline.layers,observed),...baseline.stats};
}
function cachedProperties(file,ttl){
 if(Date.now()-(fs.statSync(file,{throwIfNoEntry:false})?.mtimeMs??0)>=ttl)return null;
 try{
  const value=JSON.parse(fs.readFileSync(file));
  return Array.isArray(value?.updates)&&Array.isArray(value.finishes)?value:null;
 }catch{return null}
}
function saveProperties(file,updates,finishes,additions){
 const temp=file+'.'+crypto.randomUUID()+'.tmp';
 try{fs.writeFileSync(temp,JSON.stringify({updates,finishes,additions}));fs.renameSync(temp,file)}
 finally{fs.rmSync(temp,{force:true})}
}
export async function buildingEnrichment(x,y,cache,fetcher=fetch,baseline=null){
 let stats=baseline?.stats??{requests:0,bytes:0};
 const base={id:[x,y],release:null,attribution,updates:[],finishes:[],additions:[],additionsStatus:hasFootprintSurvey(x,y)?'unavailable':'empty'},mx=Math.floor(x/4),my=Math.floor(y/4);
 if(![x,y].every(i=>Number.isInteger(i)&&i>=0&&i<65536))return {...base,status:'unavailable',...stats};
 const miss=path.join(cache,'ov-pause-'+mx+'-'+my);
 try{
  const paused=Date.now()-(fs.statSync(miss,{throwIfNoEntry:false})?.mtimeMs??0)<90000;
  baseline??=localBuildingTile(x,y,cache,fetcher);
  const {rows:baseRows,fingerprint,read}=baseline;stats=baseline.stats;
  base.baselineFingerprint=fingerprint;
  if(!baseRows.length&&!hasFootprintSurvey(x,y))return {...base,status:'empty',...stats};
  const catalogKey='ov-catalog-v1';
  // A failed neighboring archive read needn't hide an already cached district.
  const catalogFile=path.join(cache,catalogKey);
  if(paused&&!fs.existsSync(catalogFile))return {...base,status:'unavailable',...stats};
  const catalogBytes=paused?fs.readFileSync(catalogFile):await read(catalogKey,catalog,day,null,2200);
  const release=JSON.parse(catalogBytes.toString()).latest;
  if(!/^20\d\d-\d\d-\d\d\.\d{1,3}$/.test(release))throw Error('Invalid Overture release');
  base.release=release;
  const surveyVersion=footprintSurveyVersion(x,y);
  const key=(surveyVersion?'ov-verified-v1-'+surveyVersion+'-':'ov-properties-v5-')+x+'-'+y+'-'+release+'-'+fingerprint,entry=path.join(cache,key);
  const complete=cachedProperties(entry,7*day);
  if(complete)return {...base,...complete,additionsStatus:complete.additions?.length?'available':'empty',status:complete.updates.length||complete.finishes.length?'available':'empty',...stats};
  const partial=cachedProperties(entry+'.partial',90000);
  if(partial)return {...base,...partial,additionsStatus:partial.additions?.length?'available':'empty',status:'unavailable',...stats};
  if(paused)return {...base,status:'unavailable',...stats};
  const bytes=await archiveTile(mx,my,release,read);
  const observed=bytes?records(vectorTile(bytes).layers.building,x,y):[];
  const additions=verifiedFootprints(x,y,baseline.layers,observed);
  let verifyFailed=false;
  const matches=footprintMatches(baseRows,observed),finishes=surfaceUpdates(matches);
  const updates=await heightUpdates(matches,async()=>{
   try{return await baseline.getBuildingTags()}
   catch{verifyFailed=true;return new Map()}
  });
  // Keep independently sourced finish observations usable during a transient
  // tag-replica failure. A partial response can never overwrite a verified one.
  saveProperties(verifyFailed?entry+'.partial':entry,updates,finishes,additions);
  return {...base,updates,finishes,additions,additionsStatus:additions.length?'available':'empty',status:verifyFailed?'unavailable':updates.length||finishes.length?'available':'empty',...stats};
 }catch{
  fs.writeFileSync(miss,'');return {...base,status:'unavailable',...stats};
 }
}
