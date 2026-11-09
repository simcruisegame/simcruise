import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {curbsideClearance} from './municipal-road-source.mjs';
import {cacheReader} from './atlas-cache.mjs';

export const sources = [{
  id:'cambridge-2024', stemProvider:'cambridge-ma', year:2024,
  extent:[-7921541.134,5213973.259,-7910827.874,5221667.520],
  title:'Cambridge GIS / University of Vermont Spatial Analysis Laboratory 2024',
  layer:'https://services1.arcgis.com/WnzC35krSYGuYov4/arcgis/rest/services/Tree_Canopy_2024/FeatureServer/0',
  documentation:'https://www.cambridgema.gov/gis/gisdatadictionary/environmental/environmental_treecanopy2024',
  license:'https://opendatacommons.org/licenses/pddl/1.0/',heightField:'Height_ft',heightScale:.3048,idField:'OBJECTID',
}];
const TAU=2*Math.PI, world=40075016.68557849, tileWidth=world/65536, day=86400000;
const round=v=>Math.round(v*100)/100;
const inside=(p,ring)=>{
  let contained=false;
  for(let i=0,j=ring.length-1;i<ring.length;j=i++){
    const [x,y]=ring[i],[a,b]=ring[j];
    if((y>p[1])!==(b>p[1])&&p[0]<(a-x)*(p[1]-y)/(b-y)+x)contained=!contained;
  }
  return contained;
};
const cross=(u,v)=>u[0]*v[1]-u[1]*v[0];
const overlap=(u,v)=>[u[0]-v[0],u[1]-v[1]];
function radius(ring,p,angle){
  const dir=[Math.cos(angle),Math.sin(angle)];
  let nearest=Infinity;
  for(let i=1;i<ring.length;i++){
    const a=overlap(ring[i-1],p),edge=overlap(ring[i],ring[i-1]);
    const denom=cross(dir,edge);
    if(Math.abs(denom)<1e-9)continue;
    const t=cross(a,edge)/denom,v=cross(a,dir)/denom;
    if(t>.001&&v>=0&&v<=1)nearest=Math.min(nearest,t);
  }
  return nearest;
}
export function matchOutlines(rows,stems,features,source,x,y){
  const side=tileWidth/Math.cosh(Math.PI*(1-2*(y+.5)/65536)),west=tileWidth*(x-32768),north=tileWidth*(32768-y);
  const meters=p=>[(p[0]-west)*side/tileWidth,(north-p[1])*side/tileWidth];
  const records=new Map(stems.filter(s=>s.provider===source.stemProvider).map(s=>[s.id,s]));
  const candidates=rows.filter(r=>r.length>=12&&r[11]===source.stemProvider&&r[10]==='matched'&&records.has(r[9]));
  const result=new Map(),ambiguous=new Set(),seenOwners=new Set();
  for(const feature of features){
    if(feature.geometry?.rings?.length!==1)continue;
    const outer=feature.geometry.rings[0];
    if(!Array.isArray(outer)||outer.length<6||outer.length>640||!outer.every(p=>Array.isArray(p)&&p.length>=2&&p.slice(0,2).every(Number.isFinite)))continue;
    if(outer[0][0]!==outer.at(-1)[0]||outer[0][1]!==outer.at(-1)[1])continue;
    const ring=outer.map(meters),h=Number(feature.attributes?.[source.heightField])*source.heightScale;
    if(!(h>=4&&h<=55)||!(feature.attributes?.[source.idField]>0)||!Number.isInteger(feature.attributes?.[source.idField]))continue;
    const area=Math.abs(ring.slice(1).reduce((a,p,i)=>a+cross(ring[i],p),0))/2;
    if(area<10||area>850)continue;
    const min=[Math.min(...ring.map(p=>p[0])),Math.min(...ring.map(p=>p[1]))];
    const max=[Math.max(...ring.map(p=>p[0])),Math.max(...ring.map(p=>p[1]))];
    if(Math.min(...min,side-max[0],side-max[1])<.3)continue;
    const contains=s=>{
      const p=[s.u*side,s.v*side];
      return p[0]>=min[0]&&p[0]<=max[0]&&p[1]>=min[1]&&p[1]<=max[1]&&inside(p,ring);
    };
    const owned=stems.filter(contains);
    if(owned.length!==1)continue;
    const stem=owned[0], row=candidates.find(r=>r[9]===stem.id);
    if(!row||stem.plantedAt>=Date.UTC(source.year,0,1))continue;
    if(seenOwners.has(stem.id)){ambiguous.add(stem.id);continue;}
    seenOwners.add(stem.id);
    const anchor=[row[0]*side,row[1]*side];
    if(h/row[2]<.62||h/row[2]>1.68)continue;
    const radii=Array.from({length:16},(_,i)=>radius(ring,anchor,TAU*i/16));
    if(Math.min(...radii)<1.05||Math.max(...radii)>22||Math.max(...radii)>5*Math.min(...radii))continue;
    const polarArea=radii.reduce((sum,v,i)=>sum+v*radii[(i+1)%16],0)*Math.sin(TAU/16)/2;
    if(polarArea/area<.85||polarArea/area>1.10)continue;
    result.set(stem.id,{source:source.id,year:source.year,objectId:feature.attributes[source.idField],height:round(h),radii:radii.map(round)});
  }
  for(const id of ambiguous)result.delete(id);
  return result;
}
// Query only crowns intersecting already matched city trunk coordinates. A clipped, partial or ambiguous result abstains.
export async function municipalCrownShapes(x,y,rows,stems,cache,onRequest=()=>{},fetcher=fetch){
  const none={source:null,profiles:{}};
  if(![x,y].every(v=>Number.isInteger(v)&&v>=0&&v<65536)||!Array.isArray(rows)||!Array.isArray(stems))return none;
  const west=tileWidth*(x-32768),north=tileWidth*(32768-y);
  const source=sources.find(s=>west<s.extent[2]&&west+tileWidth>s.extent[0]&&north>s.extent[1]&&north-tileWidth<s.extent[3]&&stems.some(t=>t?.provider===s.stemProvider));
  if(!source)return none;
  const matches=rows.filter(r=>Array.isArray(r)&&r.length>=12&&r[11]===source.stemProvider&&r[10]==='matched'&&typeof r[9]==='string'&&r.slice(0,2).every(v=>Number.isFinite(v)&&v>=0&&v<1)).sort((a,b)=>a[9].localeCompare(b[9]));
  if(!matches.length||matches.length>192)return none;
  const out=new Map();
  const read=cacheReader(cache,onRequest,async (url,opts)=>fetcher(url,{...opts,redirect:'error'}));
  for(let i=0;i<matches.length;i+=64){
    const group=matches.slice(i,i+64);
    const points=group.map(r=>[Math.round((west+r[0]*tileWidth)*10)/10,Math.round((north-r[1]*tileWidth)*10)/10]);
    const geometry=JSON.stringify({points,spatialReference:{wkid:3857}});
    const hash=createHash('sha256').update(geometry).digest('hex').slice(0,16);
    const key='crown-poly-v1-'+source.id+'-'+x+'-'+y+'-'+hash;
    const miss=path.join(cache,key+'.miss');
    if(Date.now()-(fs.statSync(miss,{throwIfNoEntry:false})?.mtimeMs??0)<6*3600000)continue;
    const query=new URL(source.layer+'/query');
    query.search=new URLSearchParams({f:'json',where:'1=1',geometry,geometryType:'esriGeometryMultipoint',inSR:'3857',outSR:'3857',spatialRel:'esriSpatialRelIntersects',returnGeometry:'true',outFields:source.idField+','+source.heightField,maxAllowableOffset:'.45',geometryPrecision:'1',resultRecordCount:'256'}).toString();
    let invalid=false;
    try{
      const bytes=await read(key,query.href,180*day,null,5500,448*1024);
      invalid=true;
      const data=JSON.parse(bytes);
      if(!Array.isArray(data.features)||data.exceededTransferLimit||data.features.length>256)throw Error('Incomplete crown survey');
      invalid=false;
      for(const [id,value] of matchOutlines(group,stems,data.features,source,x,y))out.set(id,value);
    }catch{
      if(invalid)fs.rmSync(path.join(cache,key),{force:true});
      fs.writeFileSync(miss,'');
    }
  }
  return {source:source.id,profiles:Object.fromEntries(out)};
}

// Extended row 12 is optional. Original peak, trunk, diameter, taxon and provenance stay in the row.
export async function applyMunicipalCrowns(x,y,rows,stems,cache,onRequest,fetcher=fetch){
 const survey=await municipalCrownShapes(x,y,rows,stems,cache,onRequest,fetcher);
 if(!survey.source)return {trees:rows,crownSurvey:null,crownProfiles:0};
 const wells=await curbsideClearance(x,y,rows,survey.profiles,cache,onRequest,fetcher);
 const trees=rows.map(row=>{
  const profile=row[10]==='matched'&&survey.profiles[row[9]];
  if(!profile)return row;
  if(wells[row[9]])profile.treeWell=wells[row[9]];
  return [...row,profile];
 });
 return {trees,crownSurvey:survey.source,crownProfiles:Object.keys(survey.profiles).length};
}
