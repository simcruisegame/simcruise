// The DEM pixel centers and vector coordinates below are globally anchored.
// This produces the same terrain vertex when either neighboring game tile asks
// for it independently. There is no dependency on which tile loaded first.
import {decode} from 'fast-png';

const GRID=64;
const DEM_ZOOM=13;
const VECTOR_ZOOM=14;
const SOURCE_SIZE=256;
const EARTH_METRES=40075016.68557849;
const BIN=36;
const PAVED=new Set(['motorway','trunk','primary','secondary','tertiary','minor']);
const limit=(x,a,b)=>Math.max(a,Math.min(x,b));
function fade(a,b,x){
 const t=limit((x-a)/(b-a),0,1);
 return t*t*(3-2*t);
}

// At a source boundary, interpolation needs the actual samples on *both* sides.
// Reuse the provider cache for those strips instead of independently clamping
// two neighboring rasters (which leaves a physical ledge at the tile edge).
async function mosaic(X,Y,center,readDEM){
 const ex=Math.floor(X/8),ey=Math.floor(Y/8),maps=new Map();
 const xs=[ex],ys=[ey];
 if(X%8===0)xs.push(ex-1);
 if(X%8===7)xs.push(ex+1);
 if(Y%8===0)ys.push(ey-1);
 if(Y%8===7)ys.push(ey+1);
 await Promise.all(xs.flatMap(x=>ys.map(async y=>{
  const tx=(x+2**DEM_ZOOM)%2**DEM_ZOOM,ty=limit(y,0,2**DEM_ZOOM-1);
  const image=x===ex&&y===ey?center:decode(await readDEM(tx,ty));
  if(image.width!==SOURCE_SIZE||image.height!==SOURCE_SIZE||image.channels<3)throw Error('Invalid Terrarium elevation tile');
  maps.set(x+','+y,image);
 })));
 function sample(ix,iy){
  const x=Math.floor(ix/SOURCE_SIZE),y=Math.floor(iy/SOURCE_SIZE),im=maps.get(x+','+y);
  // Road grading is disabled well before the vector-source edge. The source
  // center is available throughout any eligible smoothing neighborhood.
  if(!im)throw Error('Missing shared terrain sample');
  const i=((iy-y*SOURCE_SIZE)*im.width+ix-x*SOURCE_SIZE)*im.channels;
  return im.data[i]*256+im.data[i+1]+im.data[i+2]/256-32768;
 }
 return (wx,wy)=>{
  const px=wx*32-.5,py=wy*32-.5,ix=Math.floor(px),iy=Math.floor(py),fx=px-ix,fy=py-iy;
  const a=sample(ix,iy)*(1-fx)+sample(ix+1,iy)*fx;
  const b=sample(ix,iy+1)*(1-fx)+sample(ix+1,iy+1)*fx;
  return a*(1-fy)+b*fy;
 };
}

function collectRoads(tile,widths,X,Y,scale){
 const layer=tile.layers.transportation,bins=new Map();
 if(!layer)return bins;
 const x0=(X%4)*scale,y0=(Y%4)*scale,halo=75;
 for(let i=0;i<layer.length;i++){
  const feature=layer.feature(i),p=feature.properties;
  if(feature.type!==2)continue;
  const protectedRoad=p.brunnel==='bridge'||p.brunnel==='tunnel'||Number(p.layer??0)!==0||p.class==='ferry';
  const elevatedRoad=(p.brunnel==='bridge'||Number(p.layer??0)>0)&&(PAVED.has(p.class)||['service','track','busway'].includes(p.class));
  if(!protectedRoad&&(!PAVED.has(p.class)||['unpaved','gravel','dirt','ground','sand','grass'].includes(p.surface)))continue;
  const factor=4*scale/feature.extent,box=feature.bbox();
  if(box[2]*factor<x0-halo||box[0]*factor>x0+scale+halo||box[3]*factor<y0-halo||box[1]*factor>y0+scale+halo)continue;
  const width=widths[p.class]??8;
  const reach=width/2+(protectedRoad?22:16);
  for(const line of feature.loadGeometry()){
   for(let j=1;j<line.length;j++){
    const ax=line[j-1].x*factor,ay=line[j-1].y*factor,bx=line[j].x*factor,by=line[j].y*factor;
    const length=Math.hypot(bx-ax,by-ay);
    if(length<.1)continue;
    const segment={ax,ay,tx:(bx-ax)/length,ty:(by-ay)/length,length,width,reach,protectedRoad,elevatedRoad};
    const loX=Math.max(x0-halo,Math.min(ax,bx)-reach),hiX=Math.min(x0+scale+halo,Math.max(ax,bx)+reach);
    const loY=Math.max(y0-halo,Math.min(ay,by)-reach),hiY=Math.min(y0+scale+halo,Math.max(ay,by)+reach);
    for(let x=Math.floor(loX/BIN);x<=Math.floor(hiX/BIN);x++){
     for(let y=Math.floor(loY/BIN);y<=Math.floor(hiY/BIN);y++){
      const key=x+','+y;
      if(!bins.has(key))bins.set(key,[]);
      bins.get(key).push(segment);
     }
    }
   }
  }
 }
 return bins;
}

export async function streamedTerrain(X,Y,tile,centerDEM,widths,readDEM,measured=null){
 const legacy=await mosaic(X,Y,centerDEM,readDEM);
 const mx=Math.floor(X/4),my=Math.floor(Y/4),parent=2**VECTOR_ZOOM;
 const lat=Math.atan(Math.sinh(Math.PI*(1-2*(my+.5)/parent)));
 const scale=EARTH_METRES/65536*Math.cos(lat);
 const roads=collectRoads(tile,widths,X,Y,scale),h=[];
 const summary={version:1,conditioned:0,structureProtected:0};
 const raw=(wx,wy,countVertex=false)=>{
  const base=legacy(wx,wy);
  if(!measured)return base;
  const h=measured.sample(wx,wy);
  if(h===null)return base;
  const weight=measured.weight(wx,wy);
  if(countVertex&&weight>0)summary.measuredVertices++;
  return base+(h-base)*weight;
 };
 if(measured){
  summary.source=measured.source;summary.products=measured.versions;summary.measuredVertices=0;
 }
 const sample=(x,y)=>raw(4*mx+x/scale,4*my+y/scale);
 for(let j=0;j<=GRID;j++){
  for(let i=0;i<=GRID;i++){
   const wx=X+i/GRID,wy=Y+j/GRID,x=(wx-4*mx)*scale,y=(wy-4*my)*scale;
   const original=raw(wx,wy,true);
   // PBF clipping and source edits can differ on either side of a z14
   // boundary. Leave a shared DEM strip there and blend into it continuously.
   const edge=fade(65,110,Math.min(x,y,4*scale-x,4*scale-y));
   let sum=0,weight=0,coverage=0,protection=1,deck=1;
   if(edge>0||measured){
    const nearby=roads.get(Math.floor(x/BIN)+','+Math.floor(y/BIN))??[];
    for(const s of nearby){
     const along=(x-s.ax)*s.tx+(y-s.ay)*s.ty,t=limit(along,0,s.length);
     const qx=s.ax+t*s.tx,qy=s.ay+t*s.ty,dx=x-qx,dy=y-qy,d=Math.hypot(dx,dy);
     if(d>=s.reach)continue;
     if(s.protectedRoad){
      const fadeAtStructure=fade(s.width/2+10,s.reach,d);
      protection=Math.min(protection,fadeAtStructure);
      if(s.elevatedRoad)deck=Math.min(deck,fadeAtStructure);
      continue;
     }
     if(edge===0)continue;
     // Estimate a longitudinal road profile from the centerline DEM on a
     // 44 m baseline. Preserve grade while limiting synthetic lateral bank.
     const back=sample(qx-22*s.tx,qy-22*s.ty),front=sample(qx+22*s.tx,qy+22*s.ty),mid=sample(qx,qy);
     const nearBack=sample(qx-11*s.tx,qy-11*s.ty),nearFront=sample(qx+11*s.tx,qy+11*s.ty);
     const longitudinal=(front-back)/44;
     const lateral=(sample(qx-5*s.ty,qy+5*s.tx)-sample(qx+5*s.ty,qy-5*s.tx))/10;
     const center=(back+4*nearBack+6*mid+4*nearFront+front)/16;
     const target=center+longitudinal*(along-t)+limit(lateral,-.035,.035)*(-dx*s.ty+dy*s.tx);
     const change=1.8*Math.tanh((target-original)/1.8);
     // Steep longitudinal or transverse slopes are too ambiguous to
     // modify. Leave their source relief, with a soft confidence transition.
     const confidence=(1-fade(.11,.19,Math.abs(longitudinal)))*(1-fade(.50,.88,Math.abs(lateral)));
     const corridor=1-fade(s.width/2+6,s.reach,d);
     const w=corridor*confidence;
     sum+=change*w;weight+=w;coverage=Math.max(coverage,w);
    }
   }
   if(protection<1)summary.structureProtected++;
   // A bare-earth DEM deliberately removes bridge decks. Keep the previous
   // surface on mapped grade-separated structures, with a feathered approach.
   const base=measured&&deck<1?original+(legacy(wx,wy)-original)*(1-deck):original;
   const adjusted=base+(weight?sum/weight*edge*coverage*protection:0);
   if(Math.abs(adjusted-original)>.015)summary.conditioned++;
   h.push(Math.round(adjusted*100)/100);
  }
 }
 return {h,grid:GRID,road_grade:summary};
}
