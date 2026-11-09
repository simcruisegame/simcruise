import Pbf from 'pbf';
// Restricted 2D polygon reader for the allowlisted Cambridge roads layer. Tags follow
// Esri's published FeatureCollection.proto, https://github.com/Esri/arcgis-pbf.
function xy(tag,v,p){if(tag===1)v.x=p.readDouble();if(tag===2)v.y=p.readDouble();}
function transform(tag,v,p){
 if(tag===1)v.origin=p.readVarint();
 if(tag===2)v.scale=p.readMessage(xy,{});
 if(tag===3)v.translate=p.readMessage(xy,{});
}
function geometry(tag,v,p){
 if(tag===2)v.sizes=p.readPackedVarint();
 if(tag===3)v.ordinates=p.readPackedSVarint();
}
function feature(tag,v,p){
 if(tag===2)v.geometry=p.readMessage(geometry,{});
 if(tag===3||tag===7)throw Error('Unsupported curve or compressed surface');
}
function reference(tag,v,p){if(tag===1||tag===2)v[tag]=p.readVarint();}
function result(tag,v,p){
 if(tag===7)v.kind=p.readVarint();
 if(tag===8)v.sr=p.readMessage(reference,{});
 if(tag>=9&&tag<=11&&p.readBoolean())throw Error('Incomplete or nonplanar road features');
 if(tag===12)v.transform=p.readMessage(transform,{origin:0});
 if(tag===15){if(v.features.length>=1024)throw Error('Excess road features');v.features.push(p.readMessage(feature,{}));}
}
function query(tag,v,p){if(tag===1)p.readMessage(result,v);}
function collection(tag,v,p){if(tag===1)v.version=p.readString();if(tag===2)p.readMessage(query,v);}
export function readRoadRings(bytes){
 const data=new Pbf(Uint8Array.from(bytes)).readFields(collection,{features:[]});
 const tr=data.transform;
 if(data.version&&data.version!=='1.0')throw Error('Unknown polygon version');
 if(data.kind!==3||![data.sr?.[1],data.sr?.[2]].some(id=>id===3857||id===102100)||tr?.origin!==0||![tr?.translate?.x,tr?.translate?.y,tr?.scale?.x,tr?.scale?.y].every(Number.isFinite))throw Error('Unknown road grid or projection');
 if(Math.min(tr.scale.x,tr.scale.y)<=0||Math.max(tr.scale.x,tr.scale.y)>.45)throw Error('Unresolved road grid');
 const polygons=[];
 for(const {geometry:g} of data.features){
  const n=g?.sizes?.reduce((a,b)=>a+b,0);
  if(!n||n>400000||g.ordinates.length!==n*2)throw Error('Missing polygon coordinates');
  let offset=0;const rings=[];
  for(const len of g.sizes){
   let x=0,y=0;const coords=[];
   for(let i=0;i<len;i++){x+=g.ordinates[offset++];y+=g.ordinates[offset++];coords.push([tr.translate.x+tr.scale.x*x,tr.translate.y-tr.scale.y*y]);}
   if(coords.length<3)continue;
   const first=coords[0],last=coords.at(-1);if(first[0]!==last[0]||first[1]!==last[1])coords.push(first);
   let minX=Infinity,minY=Infinity,maxX=-Infinity,maxY=-Infinity;
   for(const p of coords){minX=Math.min(minX,p[0]);minY=Math.min(minY,p[1]);maxX=Math.max(maxX,p[0]);maxY=Math.max(maxY,p[1]);}
   rings.push({coords,minX,minY,maxX,maxY});
  }
  polygons.push(rings);
 }
 if(!polygons.length)throw Error('No road survey');
 return polygons;
}
export function roadClearanceMeters(point,polygons,scale){
 let nearest=Infinity;
 for(const polygon of polygons){
  let inside=false;
  for(const ring of polygon){
   if(point[0]<ring.minX-20||point[0]>ring.maxX+20||point[1]<ring.minY-20||point[1]>ring.maxY+20)continue;
   for(let i=1;i<ring.coords.length;i++){
    const [x,y]=ring.coords[i-1],[a,b]=ring.coords[i],dx=a-x,dy=b-y,t=Math.max(0,Math.min(1,((point[0]-x)*dx+(point[1]-y)*dy)/(dx*dx+dy*dy||1)));
    nearest=Math.min(nearest,(point[0]-x-t*dx)**2+(point[1]-y-t*dy)**2);
    if((y>point[1])!==(b>point[1])&&point[0]<x+(point[1]-y)*dx/dy)inside=!inside;
   }
  }
  if(inside)return 0;
 }
 return Math.sqrt(nearest)*scale;
}
// The nearest physical curb defines the inward direction. Find the *next* opposing
// edge across the same road, and corroborate the width several metres each way.
// Reject intersections, mixed centrelines and dead ends rather than estimating width.
export function observedStreetSection(p,polygons,scale){
 const segments=[];let best=Infinity,nearest=null;
 for(const poly of polygons)for(const ring of poly){
  if(p[0]<ring.minX-32||p[0]>ring.maxX+32||p[1]<ring.minY-32||p[1]>ring.maxY+32)continue;
  for(let i=1;i<ring.coords.length;i++){
   const a=ring.coords[i-1],b=ring.coords[i];if(Math.min(a[0],b[0])>p[0]+32||Math.max(a[0],b[0])<p[0]-32||Math.min(a[1],b[1])>p[1]+32||Math.max(a[1],b[1])<p[1]-32)continue;
   const x=b[0]-a[0],y=b[1]-a[1],len=x*x+y*y;if(len<1e-8)continue;
   segments.push([a,b,x,y]);
   const t=Math.max(0,Math.min(1,((p[0]-a[0])*x+(p[1]-a[1])*y)/len));
   const q=[a[0]+t*x,a[1]+t*y],d=Math.hypot(q[0]-p[0],q[1]-p[1]);
   if(d<best){best=d;nearest=q;}
  }
 }
 if(!nearest||best<.1)return null;
 const n=[(nearest[0]-p[0])/best,(nearest[1]-p[1])/best],widths=[];
 for(const shift of [-3.,0,3.]){
  const start=[p[0]-n[1]*shift/scale,p[1]+n[0]*shift/scale],hits=[];
  for(const [a,,x,y] of segments){const den=n[0]*y-n[1]*x;if(Math.abs(den)<1e-9)continue;
   const dx=a[0]-start[0],dy=a[1]-start[1],t=(dx*y-dy*x)/den,v=(dx*n[1]-dy*n[0])/den;
   if(t>=-1/scale&&t<=18/scale&&v>=0&&v<1)hits.push(t*scale);
  }
  hits.sort((a,b)=>a-b);
  if(hits.length!==2||Math.abs(hits[0]-best*scale)>.55)return null;
  widths.push(hits[1]-hits[0]);
 }
 if(Math.max(...widths)-Math.min(...widths)>.8||widths[1]<5.5||widths[1]>9.6)return null;
 return {widthM:Math.round(widths[1]*100)/100,normalEast:Math.round(n[0]*1000)/1000,normalNorth:Math.round(n[1]*1000)/1000};
}
