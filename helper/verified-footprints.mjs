import {createHash} from 'node:crypto';
import {classifyRings} from '@mapbox/vector-tile';
import earcut from 'earcut';
import surveys from './footprint-surveys.json' with {type:'json'};
import corroborated from './corroborated-outlines.json' with {type:'json'};

import {roadWidths} from './road-widths.mjs';
const eps=1e-12;
const maxInputs=2000;
const key=(x,y)=>x+'-'+y;
export const hasFootprintSurvey=(x,y)=>Object.hasOwn(surveys.tiles,key(x,y)) || Object.hasOwn(corroborated.tiles,key(x,y));
const versions=new Map(Object.entries(surveys.tiles).map(([id,rows])=>{
 const sources=Object.values(rows).map(entry=>surveys.sources[entry.survey]);
 const hash=createHash('sha256').update(JSON.stringify([rows,sources])).digest('hex');
 return [id,hash.slice(0,12)];
}));
for(const [tile,evidence] of Object.entries(corroborated.tiles)){
 const combined=JSON.stringify([versions.get(tile)??null,evidence]);
 versions.set(tile,createHash('sha256').update(combined).digest('hex').slice(0,12));
}
export const footprintSurveyVersion=(x,y)=>versions.get(key(x,y));
const digest=ring=>createHash('sha256').update(JSON.stringify(ring)).digest('hex');
const box=points=>points.reduce((b,[x,y])=>[Math.min(b[0],x),Math.min(b[1],y),Math.max(b[2],x),Math.max(b[3],y)],[Infinity,Infinity,-Infinity,-Infinity]);
const near=(a,b,d)=>a[0]<=b[2]+d && b[0]<=a[2]+d && a[1]<=b[3]+d && b[1]<=a[3]+d;
const cross=(p,q,r)=>(q[0]-p[0])*(r[1]-p[1])-(q[1]-p[1])*(r[0]-p[0]);
function inside(p,polygon){
 let result=false;
 for(let i=0,j=polygon.length-1;i<polygon.length;j=i++){
  const a=polygon[j],b=polygon[i];
  if((a[1]>p[1])!==(b[1]>p[1]) && p[0]<(b[0]-a[0])*(p[1]-a[1])/(b[1]-a[1])+a[0])result=!result;
 }
 return result;
}
function distance2(p,a,b){
 const dx=b[0]-a[0],dy=b[1]-a[1];
 const t=Math.max(0,Math.min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/(dx*dx+dy*dy || 1)));
 return (p[0]-a[0]-dx*t)**2+(p[1]-a[1]-dy*t)**2;
}
function conflicts(candidate,line,margin,closed){
 if(!near(candidate.box,box(line),margin))return false;
 const ring=candidate.rings[0];
 if(inside(line[0],ring) || (closed && inside(ring[0],line)))return true;
 for(let i=0;i<ring.length;i++){
  const a=ring[i],b=ring[(i+1)%ring.length];
  for(let j=0;j<line.length-(closed?0:1);j++){
   const c=line[j],d=line[(j+1)%line.length];
   if(cross(a,b,c)*cross(a,b,d)<0 && cross(c,d,a)*cross(c,d,b)<0)return true;
   if(Math.min(distance2(a,c,d),distance2(b,c,d),distance2(c,a,b),distance2(d,a,b))<=margin*margin+eps)return true;
  }
 }
 return false;
}
function occupied(layers,candidate,x,y,side){
 const {building,transportation}=layers;
 if(!transportation || (building && building.length>15000) || transportation.length>15000)return true;
 const padding=12/side;
 for(const [layer,closed] of [[building,true],[transportation,false]]){
  if(!layer)continue;
  for(let i=0;i<layer.length;i++){
   const f=layer.feature(i),q=f.extent/4,sx=x%4,sy=y%4;
   if((closed && f.type!==3)||(!closed && f.type!==2))continue;
   const fb=f.bbox();
   if(!near(candidate.box,[fb[0]/q-sx,fb[1]/q-sy,fb[2]/q-sx,fb[3]/q-sy],padding))continue;
   // Check all building parts, hidden buildings and outer rings, including
   // shapes owned by another game tile. For roads use the game's full width.
   const width=closed?0:roadWidths.of(f);
   if(!closed && (!width || f.properties.brunnel==='tunnel'))continue;
   const lines=closed?classifyRings(f.loadGeometry()).map(poly=>poly[0]):f.loadGeometry();
   for(const line of lines){
    const local=line.map(p=>[p.x/q-sx,p.y/q-sy]);
    if(local.length>256 && near(box(local),candidate.box,padding))return true;
    if(local.length>=2 && conflicts(candidate,local,(closed?2:width*.5+1.5)/side,closed))return true;
   }
  }
 }
 return false;
}

// Detection can produce source candidates that fail both independent evidence checks.
export function footprintCandidates(x,y,layers,observed,certificates=null){
 if(!layers || observed.length>maxInputs)return [];
 const side=611.496226/Math.cosh(Math.PI*(1-2*(y+.5)/65536));
 const result=[];
 for(const row of observed){
  const p=row.props,ring=row.rings[0];
  if(certificates && !Object.hasOwn(certificates,p.id))continue;
  if(typeof p.id!=='string' || !/^[a-f0-9-]{36}$/.test(p.id) || p.has_parts!==false || p.is_underground!==false || row.rings.length!==1 || ring.length<3 || ring.length>96)continue;
  if(Math.min(row.box[0],row.box[1],1-row.box[2],1-row.box[3])*side<3)continue;
  const area=Math.abs(ring.reduce((sum,a,i)=>sum+cross([0,0],a,ring[(i+1)%ring.length]),0))*.5*side**2;
  if(area<16||area>2000)continue;
  let sources;try{sources=JSON.parse(p.sources)}catch{continue}
  if(!Array.isArray(sources)||sources.some(s=>!s||typeof s.property!=='string'))continue;
  const roots=sources.filter(s=>s.property==='');
  if(roots.length!==1)continue;
  const source=roots[0];
  if(source.license!=='ODbL-1.0'||p['@geometry_source']!==source.dataset||!['OpenStreetMap','Microsoft ML Buildings'].includes(source.dataset))continue;
  if(sources.some(s=>s.property==='/geometry' && (s.dataset!==source.dataset || s.license!==source.license)))continue;
  if(observed.some(other=>other!==row && (other.props.id===p.id || (near(other.box,row.box,1.5/side)&&conflicts(row,other.rings[0],1.5/side,true)))))continue;
  if(occupied(layers,row,x,y,side))continue;
  result.push({id:p.id,ring,outlineHash:digest(ring),geometrySource:source.dataset,license:source.license,sourceVersion:source.version??null,
   confidence:source.confidence??null,estimatedHeight:p.height??null,
   estimateOwner:p['@height_source']===source.dataset && !sources.some(s=>s.property==='/height' && (s.dataset!==source.dataset || s.license!==source.license))?source.dataset:null});
  if(result.length===64)break;
 }
 return result;
}

function measuredFootprints(x,y,layers,observed,certificates){
 if(!certificates)return [];
 const additions=[];
 for(const f of footprintCandidates(x,y,layers,observed,certificates)){
  const evidence=certificates[f.id];
  if(evidence.outlineHash!==f.outlineHash || evidence.geometrySource!==f.geometrySource || !surveys.sources[evidence.survey])continue;
  const h=evidence.heightM;
  if(!(Number.isFinite(h)&&h>=2&&h<=85 && evidence.roofReturns>=120 && evidence.groundReturns>=45 &&
    evidence.coveredCells>=5 && evidence.coveredCells>=.8*evidence.eligibleCells))continue;
  const triangles=earcut(f.ring.flat(),[],2);
  if(triangles.length!==3*(f.ring.length-2))continue;
  const center=f.ring.reduce((c,a)=>[c[0]+a[0]/f.ring.length,c[1]+a[1]/f.ring.length],[0,0]);
  const source=surveys.sources[evidence.survey];
  additions.push({id:f.id,ring:f.ring,triangles,center,heightM:h,geometrySource:f.geometrySource,license:f.license,
   survey:evidence.survey,acquisition:source.acquisition,source:source.url,roofReturns:evidence.roofReturns,
   groundReturns:evidence.groundReturns,coveredCells:evidence.coveredCells,eligibleCells:evidence.eligibleCells});
 }
 return additions.sort((a,b)=>a.id.localeCompare(b.id));
}

function corroboratedFootprints(x,y,layers,observed,district,measured){
 if(!district?.features || district.registration?.pass!==true)return [];
 const result=[];
 for(const f of footprintCandidates(x,y,layers,observed,district.features)){
  const record=district.features[f.id];
  if(measured.has(f.id) || f.geometrySource!=='Microsoft ML Buildings' || record.outlineHash!==f.outlineHash ||
   record.source!=='ORNL' || !['Automated','Manual','Spatial Intersect'].includes(record.validationMethod) ||
   !(record.iou>=.70 && record.iou<=1 && record.centroidM>=0 && record.centroidM<=3))continue;
  const images=record.images;
  if(!/^\d{4}-\d{2}-\d{2}$/.test(record.sourceDate) || !Array.isArray(images) || images.length!==2 ||
   new Set(images.map(o=>o.date?.slice(0,4))).size!==2 || images.some(o=>!o.item || !/^\d{4}-\d{2}-\d{2}$/.test(o.date) ||
    o.date===record.sourceDate || !(o.edges>=3 && o.edgeFraction>=.65)))continue;
  const triangles=earcut(f.ring.flat(),[],2);
  if(triangles.length!==3*(f.ring.length-2))continue;
  const center=f.ring.reduce((sum,p)=>[sum[0]+p[0]/f.ring.length,sum[1]+p[1]/f.ring.length],[0,0]);
  const estimated=f.estimateOwner==='Microsoft ML Buildings' && Number.isFinite(f.estimatedHeight) && f.estimatedHeight>=2.8 && f.estimatedHeight<=35;
  result.push({id:f.id,ring:f.ring,triangles,center,geometryStatus:'corroborated-outline',geometrySource:f.geometrySource,license:f.license,
   geometryWitness:'ORNL/FEMA USA Structures (CC BY 4.0)',geometrySourceDate:record.sourceDate,aerialEvidence:images,
   geometryAgreement:record.iou,outlineEvidenceRelease:district.release,eavesM:null,heightStatus:estimated?'source-model':'unknown',
   displayHeightM:estimated?f.estimatedHeight:3.0,displayHeightSource:estimated?f.estimateOwner:'visual assumption'});
 }
 return result;
}

export function verifiedFootprints(x,y,layers,observed,certificates=surveys.tiles[key(x,y)],district=corroborated.tiles[key(x,y)]){
 const measured=measuredFootprints(x,y,layers,observed,certificates);
 if(!district)return measured;
 const measuredIds=new Set(measured.map(row=>row.id));
 return [...measured,...corroboratedFootprints(x,y,layers,observed,district,measuredIds)].sort((a,b)=>a.id.localeCompare(b.id));
}
