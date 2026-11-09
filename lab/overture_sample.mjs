// Read-only, bounded comparison between the public Overture visualization
// archive and the OpenFreeMap footprint tiles consumed by atlas.mjs.
// Run from the source project after installing its ordinary npm dependencies:
// node lab/overture_sample.mjs [latitude longitude heading_degrees]
// Fixed dates below pin the comparison, not the feature observation dates.
import {gunzipSync} from 'node:zlib';
import {VectorTile,classifyRings} from '@mapbox/vector-tile';
import Pbf from 'pbf';
const release='2026-10-21.0',baseline='20261101_080013_pt';
const archive='https://overturemaps-extras-us-west-2.s3.us-west-2.amazonaws.com/tiles/'+release+'/buildings.pmtiles';
const counts={overture:{requests:0,bytes:0},baseline:{requests:0,bytes:0,bodyBytesAfterHTTPDecoding:0}};
const memo=new Map();
async function readRange(start,length){
 const key=start+':'+length;if(memo.has(key))return memo.get(key);
 if(!Number.isSafeInteger(start)||!Number.isInteger(length)||length<=0||length>2*1024*1024)throw Error('Archive range outside budget');
 const response=await fetch(archive,{headers:{Range:'bytes='+start+'-'+(start+length-1)},signal:AbortSignal.timeout(7000)});
 if(response.status!==206||!response.headers.get('content-range')?.startsWith('bytes '+start+'-'+(start+length-1)+'/')){await response.body?.cancel();throw Error('Range request unsupported')}
 const bytes=Buffer.from(await response.arrayBuffer());if(bytes.length!==length)throw Error('Incomplete range');
 counts.overture.requests++;counts.overture.bytes+=bytes.length;memo.set(key,bytes);return bytes;
}
function uncompress(data,code){if(code===1)return data;if(code===2)return gunzipSync(data);throw Error('Unsupported compression '+code)}
function directory(buffer){
 let at=0;
 function integer(){let n=0,p=1;while(at<buffer.length){const b=buffer[at++];n+=(b&127)*p;if(b<128)return n;p*=128;if(!Number.isSafeInteger(p))break}throw Error('Invalid PMTiles directory integer')}
 const n=integer(),rows=[];let id=0;
 for(let i=0;i<n;i++){id+=integer();rows.push({id})}
 for(const r of rows)r.run=integer();
 for(const r of rows)r.length=integer();
 for(let i=0;i<n;i++){const v=integer();rows[i].offset=v===0&&i?rows[i-1].offset+rows[i-1].length:v-1}
 return rows;
}
function hilbert(z,x,y){
 let d=(4**z-1)/3;
 for(let s=2**(z-1);s>=1;s/=2){
  const a=(x&s)?1:0,b=(y&s)?1:0;d+=s*s*((3*a)^b);
  if(!b){if(a){x=s-1-x;y=s-1-y}[x,y]=[y,x]}
 }
 return d;
}
const header=await readRange(0,16384);
if(header.toString('ascii',0,7)!=='PMTiles'||header[7]!==3)throw Error('Expected PMTiles v3');
const offset=pos=>Number(header.readBigUInt64LE(pos));
const rootOffset=offset(8),rootLength=offset(16),internal=header[97],tileCompression=header[98];
const rootData=rootOffset+rootLength<=header.length?header.subarray(rootOffset,rootOffset+rootLength):await readRange(rootOffset,rootLength);
const root=directory(uncompress(rootData,internal));
async function getTile(x,y){
 const id=hilbert(14,x,y);let entries=root;
 for(let depth=0;depth<4;depth++){
  let l=0,r=entries.length-1;
  while(l<=r){const i=(l+r)>>1;if(entries[i].id<=id)l=i+1;else r=i-1}
  const row=entries[r];if(!row)return null;
  if(row.run){if(id>=row.id+row.run)return null;return uncompress(await readRange(offset(56)+row.offset,row.length),tileCompression)}
  entries=directory(uncompress(await readRange(offset(40)+row.offset,row.length),internal));
 }
 throw Error('Unexpected PMTiles nesting');
}
async function baseTile(x,y){
 const response=await fetch('https://tiles.openfreemap.org/planet/'+baseline+'/14/'+x+'/'+y+'.pbf',{signal:AbortSignal.timeout(7000)});
 if(!response.ok || Number(response.headers.get('content-length'))>2*1024*1024)throw Error('Baseline tile unavailable or too large');
 let b=Buffer.from(await response.arrayBuffer());if(b.length>2*1024*1024)throw Error('Baseline exceeds budget');
 counts.baseline.requests++;counts.baseline.bytes+=Number(response.headers.get('content-length'))||b.length;counts.baseline.bodyBytesAfterHTTPDecoding+=b.length;
 return b[0]===31?gunzipSync(b):b;
}
function cross(a,b){return a[0]*b[1]-a[1]*b[0]}
function centroid(ring){
 let s=0,x=0,y=0;for(let i=0;i<ring.length;i++){const a=ring[i],b=ring[(i+1)%ring.length],v=cross(a,b);s+=v;x+=(a[0]+b[0])*v;y+=(a[1]+b[1])*v}
 return Math.abs(s)<1e-7?ring[0]:[x/(3*s),y/(3*s)];
}
function rect(points){return [Math.min(...points.map(p=>p[0])),Math.min(...points.map(p=>p[1])),Math.max(...points.map(p=>p[0])),Math.max(...points.map(p=>p[1]))]}
function inside(rings,x,y){
 let odd=false;
 for(const ring of rings)for(let i=0,j=ring.length-1;i<ring.length;j=i++){
  const a=ring[i],b=ring[j];if((a[1]>y)!==(b[1]>y)&&x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0])odd=!odd;
 }
 return odd;
}
// A fixed 48x48 union-bounds raster estimates IoU for these small samples.
// Reject duplicates in *both* directions; this is evidence of shape agreement.
function overlap(a,b){
 const A=a.box,B=b.box;
 if(A[0]>=B[2]||B[0]>=A[2]||A[1]>=B[3]||B[1]>=A[3])return 0;
 const bounds=[Math.min(A[0],B[0]),Math.min(A[1],B[1]),Math.max(A[2],B[2]),Math.max(A[3],B[3])];
 let intersection=0,union=0;
 for(let j=0;j<48;j++)for(let i=0;i<48;i++){
  const x=bounds[0]+(i+.5)*(bounds[2]-bounds[0])/48,y=bounds[1]+(j+.5)*(bounds[3]-bounds[1])/48;
  const l=inside(a.rings,x,y),r=inside(b.rings,x,y);if(l||r)union++;if(l&&r)intersection++;
 }
 return union?intersection/union:0;
}
const quant=(a,p)=>a.length?[...a].sort((x,y)=>x-y)[Math.floor((a.length-1)*p)]:null;
const fields=['height','num_floors','roof_height','roof_shape','roof_color','roof_material','facade_color','facade_material'];
function heightRecord(props){
 let rows=[];
 try{rows=JSON.parse(props.sources??'[]')}catch{}
 const property=rows.find(s=>s.property==='/properties/height'||s.property==='/height');
 const dataset=property?.dataset??props['@height_source']??'unspecified';
 return {dataset,propertySpecific:!!property,sourceRecordUpdate:property?.update_time??null};
}
function footprintArea(f){
 return f.rings.reduce((area,ring,k)=>{
  let a=0;for(let i=0;i<ring.length;i++)a+=cross(ring[i],ring[(i+1)%ring.length]);
  return area+(k===0?1:-1)*Math.abs(a)*.5;
 },0);
}
function summary(features){
 const has={};for(const field of fields)has[field]=features.filter(f=>f.props[field]!=null&&f.props[field]!=='').length;
 const heightSources={},geometrySources={};
 for(const {props} of features){
  const geom=props['@geometry_source']??'unspecified';geometrySources[geom]=(geometrySources[geom]??0)+1;
  if(props.height!=null){const src=heightRecord(props).dataset;heightSources[src]=(heightSources[src]??0)+1}
 }
 return {features:features.length,nonNull:has,heightSources,geometrySources};
}
async function compare(name,lat,lon,heading){
 const before=JSON.parse(JSON.stringify(counts));
 const tileX=(lon+180)/360*16384,tileY=(1-Math.asinh(Math.tan(lat*Math.PI/180))/Math.PI)*8192;
 const side=40075016.68557849*Math.cos(lat*Math.PI/180)/16384;
 const radius=145,margin=radius/side;
 const all={over:[],base:[],parts:[]};
 const seen={over:new Set(),parts:new Set(),base:new Set()};
 for(let x=Math.floor(tileX-margin);x<=Math.floor(tileX+margin);x++)for(let y=Math.floor(tileY-margin);y<=Math.floor(tileY+margin);y++){
  const [over,base]=await Promise.all([getTile(x,y),baseTile(x,y)]);
  for(const [source,buffer,layerName] of [['base',base,'building'],['over',over,'building'],['parts',over,'building_part']]){
   if(!buffer)continue;
   const layer=new VectorTile(new Pbf(buffer)).layers[layerName];if(!layer)continue;
   for(let i=0;i<layer.length;i++){
    const feature=layer.feature(i),props=feature.properties;
    if(feature.type!==3||(source==='base'&&(props.hide_3d||Number(props.render_min_height)>0)))continue;
    for(const [k,poly] of classifyRings(feature.loadGeometry()).entries()){
     const rings=poly.map(r=>r.slice(0,-1).map(p=>[(x+p.x/feature.extent-tileX)*side,(y+p.y/feature.extent-tileY)*side]));
     if(!rings[0]?.length)continue;
     const center=centroid(rings[0]),dist=Math.hypot(...center);
     if(dist>radius)continue;
     const bearing=(Math.atan2(center[0],-center[1])*180/Math.PI+360)%360;
     const inView=dist<=20||Math.abs((bearing-heading+540)%360-180)<=50;
     const key=props.id??feature.id;const id=key==null?x+'/'+y+'/'+i+'/'+k:key+':'+k;
     if(seen[source].has(id))continue;seen[source].add(id);
     all[source].push({rings,props,center,box:rect(rings[0]),inView});
    }
   }
  }
 }
 function sampled(narrow){
  const over=all.over.filter(f=>!narrow||f.inView),base=all.base.filter(f=>!narrow||f.inView),parts=all.parts.filter(f=>!narrow||f.inView);
  const candidates=over.flatMap((f,i)=>base.map((g,j)=>[i,j,overlap(f,g)]).filter(r=>r[2]>=.82));
  const matches=candidates.filter(([i,j])=>candidates.filter(t=>t[0]===i||t[1]===j).length===1);
  const separate=over.filter(f=>!base.some(g=>overlap(f,g)>.15));
  const paired=matches.map(([i,j])=>[over[i],base[j]]),both=paired.filter(([f])=>f.props.height!=null);
  const diffs=both.map(([f,g])=>+Math.abs(f.props.height-(g.props.render_height||5)).toFixed(2));
  return {over:summary(over),parts:summary(parts),baselineFootprints:base.length,paired:matches.length,pairedWithHeight:both.length,
   bothHeightDeltaMedian:quant(diffs,.5),bothHeightDeltaAtLeast2m:diffs.filter(d=>d>=2).length,
   pairedWithFacadeOrRoofFinish:paired.filter(([f])=>['roof_color','roof_material','facade_color','facade_material'].some(k=>f.props[k]!=null)).length,
   unmatchedWithHeights:over.filter((f,i)=>f.props.height!=null&&!matches.some(m=>m[0]===i)).length,
   overWithNoBaselineNearOverlap:separate.length,
   separateExamples:separate.slice(0,4).map(f=>({id:f.props.id,height:f.props.height,areaM2:Math.round(footprintArea(f)),heightRecord:heightRecord(f.props),geometrySource:f.props['@geometry_source']})),
   examples:both.slice(0,4).map(([f,g])=>({id:f.props.id,reportedHeight:f.props.height,heightRecord:heightRecord(f.props),baseline:g.props.render_height||5,sourceRecord:f.props.sources}))};
 }
 const lookup={};for(const service of ['overture','baseline'])lookup[service]={requests:counts[service].requests-before[service].requests,bytes:counts[service].bytes-before[service].bytes};
 return {name,center:[lat,lon],heading,radiusM:radius,lookupExcludesSharedHeader:lookup,forwardCone:sampled(true),surrounding:sampled(false)};
}
const args=process.argv.slice(2).map(Number);
const sites=args.length===3?[['custom',...args]]:[['Miami',25.748401,-80.208896,52.21],['Cambridge',42.365183,-71.134311,0]];
const result={release,baseline,sites:[],transferred:counts};
for(const row of sites)result.sites.push(await compare(...row));
console.log(JSON.stringify(result,null,2));
