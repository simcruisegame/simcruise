// The DEM pixel centers and vector coordinates below are globally anchored.
// This produces the same terrain vertex when either neighboring game tile asks
// for it independently. There is no dependency on which tile loaded first.
import {decode} from 'fast-png';

const GRID=64;
// Output grid. The coarse 64 grid is upsampled exactly onto its own triangles,
// so a fine vertex differs from the coarse surface only where a road bench
// changed it, and the game's adaptive mesh splits only those cells.
const FINE=128;
const DEM_ZOOM=13;
const VECTOR_ZOOM=14;
const SOURCE_SIZE=256;
const EARTH_METRES=40075016.68557849;
const BIN=16;
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

// ---------------------------------------------------------------------------
// Road benches. A road is a shelf with a smooth longitudinal profile, a cross
// slope and earthwork beside it: cut and fill slopes that meet the natural
// ground at a limited batter. A 10 m elevation model cannot see that shelf on a
// mountainside; sampled on the centreline it wanders by metres as the road
// crosses the slope, and point-sampling a terrain grid makes every grid edge a
// slope step. So every paved road is rebuilt as a bench:
//  1. Road pieces from this z14 vector tile, and from a neighbour when a road
//     crosses into it near this tile, are clipped to their own tiles (so each
//     piece exists once) and chained through junctions.
//  2. Each chain's centreline is sampled every 2 m along its own arc length.
//     A Gaussian local-linear fit (sigma 10 m) with one Tukey robustness pass
//     gives the profile; curvature gives superelevation toward the inside of
//     curves (AASHTO-style e = V²/127R, halved for side friction, ≤ 6 %).
//  3. Fine-grid vertices within the bench take the profile and cross slope
//     exactly; beyond it the natural ground is clamped into cut (1.2:1) and
//     fill (0.7:1) slopes, fading out 25 m past the bench.
// Every input is a function of world position (the vector geometry, the shared
// DEM inside the measured apron), never of which tile asked first, so two
// neighbouring tiles compute identical heights on their shared edge.
const STEP=2,SIGMA=16,WINDOW=3*SIGMA,TUKEY=3.5;
const CUT=1.2,FILL=.7,BATTER_REACH=25;
const SAMPLE_APRON=150; // metres around the tile; usgs-ground reads 120 m
const MERGE=2; // endpoint tolerance (m) when chaining pieces
const LOOSE=new Set(['unpaved','gravel','dirt','ground','sand','grass']);

function clipSegment(ax,ay,bx,by,x0,y0,x1,y1){
 let t0=0,t1=1;const dx=bx-ax,dy=by-ay;
 for(const [p,q] of [[-dx,ax-x0],[dx,x1-ax],[-dy,ay-y0],[dy,y1-ay]]){
  if(p===0){if(q<0)return null;continue}
  const t=q/p;if(p<0)t0=Math.max(t0,t);else t1=Math.min(t1,t);
  if(t0>t1)return null;
 }
 return [ax+t0*dx,ay+t0*dy,ax+t1*dx,ay+t1*dy];
}

// Lines of one z14 tile in metres relative to the requesting tile's parent.
// kind: 'bench' (paved, at grade), 'protected' (bridge, tunnel, layer != 0).
function tileLines(tile,widths,ox,oy,scale,clipToOwn){
 const out=[],layer=tile?.layers?.transportation;
 if(!layer)return out;
 const size=4*scale;
 for(let i=0;i<layer.length;i++){
  const feature=layer.feature(i),p=feature.properties;
  if(feature.type!==2)continue;
  const protectedRoad=p.brunnel==='bridge'||p.brunnel==='tunnel'||Number(p.layer??0)!==0||p.class==='ferry';
  const elevatedRoad=(p.brunnel==='bridge'||Number(p.layer??0)>0)&&(PAVED.has(p.class)||['service','track','busway'].includes(p.class));
  if(!protectedRoad&&(!PAVED.has(p.class)||LOOSE.has(p.surface)))continue;
  const width=widths[p.class]??8,f=size/feature.extent;
  for(const line of feature.loadGeometry()){
   let piece=[];
   const flush=()=>{if(piece.length>=2)out.push({pts:piece,width,protectedRoad,elevatedRoad,cls:p.class,name:p.name??''});piece=[]};
   for(let j=1;j<line.length;j++){
    const ax=ox+line[j-1].x*f,ay=oy+line[j-1].y*f,bx=ox+line[j].x*f,by=oy+line[j].y*f;
    const c=clipToOwn?clipSegment(ax,ay,bx,by,ox,oy,ox+size,oy+size):[ax,ay,bx,by];
    if(!c){flush();continue}
    const last=piece[piece.length-1];
    if(!last||Math.hypot(last[0]-c[0],last[1]-c[1])>1e-6){flush();piece.push([c[0],c[1]])}
    piece.push([c[2],c[3]]);
    if(c[2]!==bx||c[3]!==by)flush();
   }
   flush();
  }
 }
 return out;
}

function arcLength(pts){
 const s=[0];
 for(let k=1;k<pts.length;k++)s.push(s[k-1]+Math.hypot(pts[k][0]-pts[k-1][0],pts[k][1]-pts[k-1][1]));
 return s;
}

// Join pieces end to end. Two ends meeting alone are joined; at a junction the
// straightest pair joins, preferring the same name and class.
function chainPieces(pieces){
 const ends=[];
 pieces.forEach((piece,i)=>{
  const a=piece.pts[0],b=piece.pts[piece.pts.length-1];
  const a2=piece.pts[1],b2=piece.pts[piece.pts.length-2];
  ends.push({piece:i,at:0,x:a[0],y:a[1],dx:a2[0]-a[0],dy:a2[1]-a[1]});
  ends.push({piece:i,at:1,x:b[0],y:b[1],dx:b2[0]-b[0],dy:b2[1]-b[1]});
 });
 const grid=new Map(),key=(x,y)=>Math.floor(x/MERGE)+','+Math.floor(y/MERGE);
 ends.forEach((e,i)=>{const k=key(e.x,e.y);if(!grid.has(k))grid.set(k,[]);grid.get(k).push(i)});
 const link=new Array(ends.length).fill(-1),seen=new Set();
 ends.forEach((e,i)=>{
  if(seen.has(i))return;
  const cluster=[];
  const gx=Math.floor(e.x/MERGE),gy=Math.floor(e.y/MERGE);
  for(let x=gx-1;x<=gx+1;x++)for(let y=gy-1;y<=gy+1;y++)for(const j of grid.get(x+','+y)??[]){
   if(!seen.has(j)&&Math.hypot(ends[j].x-e.x,ends[j].y-e.y)<=MERGE)cluster.push(j);
  }
  cluster.forEach(j=>seen.add(j));
  const pairs=[];
  for(let a=0;a<cluster.length;a++)for(let b=a+1;b<cluster.length;b++){
   const A=ends[cluster[a]],B=ends[cluster[b]];
   if(A.piece===B.piece)continue;
   const la=Math.hypot(A.dx,A.dy)||1,lb=Math.hypot(B.dx,B.dy)||1;
   const straight=-(A.dx*B.dx+A.dy*B.dy)/(la*lb); // 1 = continues straight on
   const pieceA=pieces[A.piece],pieceB=pieces[B.piece];
   const same=(pieceA.name===pieceB.name?.3:0)+(pieceA.cls===pieceB.cls?.2:0);
   if(cluster.length>2&&straight<.5)continue;
   pairs.push([straight+same,cluster[a],cluster[b]]);
  }
  pairs.sort((p,q)=>q[0]-p[0]);
  for(const [,a,b] of pairs)if(link[a]<0&&link[b]<0){link[a]=b;link[b]=a}
 });
 // Walk chains. Each piece keeps its own orientation and arc length, so the
 // sample positions along it do not depend on where its chain begins.
 const used=new Array(pieces.length).fill(false),chains=[];
 const walk=(startEnd)=>{
  const parts=[];let end=startEnd,offset=0;
  while(end>=0){
   const e=ends[end],piece=pieces[e.piece];
   if(used[e.piece])break;
   used[e.piece]=true;
   const s=arcLength(piece.pts),length=s[s.length-1];
   parts.push({piece,s,length,reversed:e.at===1,offset});
   offset+=length;
   const far=end^1;
   end=link[far];
  }
  return parts;
 };
 ends.forEach((e,i)=>{if(!used[e.piece]&&link[i]<0)chains.push(walk(i))});
 ends.forEach((e,i)=>{if(!used[e.piece]&&e.at===0)chains.push(walk(i))}); // closed loops
 return chains;
}

function gaussianLinear(S,Z,W,s){
 let sw=0,swx=0,swy=0,swxx=0,swxy=0;
 let lo=0,hi=S.length;
 while(lo<hi){const m=(lo+hi)>>1;if(S[m]<s-WINDOW)lo=m+1;else hi=m}
 for(let i=lo;i<S.length&&S[i]<=s+WINDOW;i++){
  if(!(W[i]>0))continue;
  const x=S[i]-s,w=W[i]*Math.exp(-.5*x*x/(SIGMA*SIGMA));
  sw+=w;swx+=w*x;swy+=w*Z[i];swxx+=w*x*x;swxy+=w*x*Z[i];
 }
 if(sw<=1e-9)return NaN;
 const det=sw*swxx-swx*swx;
 if(det<=1e-9*Math.max(sw*swxx,1e-12))return swy/sw;
 return (swy-swx*(sw*swxy-swx*swy)/det)/sw;
}

// Samples, robust weights, profile and superelevation for one chain.
function chainProfile(parts,sample,inside,designSpeed){
 const rows=[];
 for(const part of parts){
  const {piece,s,length,reversed,offset}=part;
  let seg=0;
  for(let l=0;l<=length+1e-9;l+=STEP){
   while(seg<s.length-2&&l>s[seg+1])seg++;
   const a=piece.pts[seg],b=piece.pts[seg+1],t=(l-s[seg])/Math.max(s[seg+1]-s[seg],1e-9);
   const x=a[0]+(b[0]-a[0])*t,y=a[1]+(b[1]-a[1])*t;
   rows.push([offset+(reversed?length-l:l),x,y]);
  }
 }
 rows.sort((p,q)=>p[0]-q[0]);
 const S=new Float64Array(rows.length),Z=new Float64Array(rows.length),W=new Float64Array(rows.length);
 rows.forEach((r,i)=>{S[i]=r[0];if(inside(r[1],r[2])){Z[i]=sample(r[1],r[2]);W[i]=Number.isFinite(Z[i])?1:0}});
 const first=new Float64Array(rows.length);
 for(let i=0;i<rows.length;i++)first[i]=W[i]?gaussianLinear(S,Z,W,S[i]):NaN;
 const R=new Float64Array(rows.length);
 for(let i=0;i<rows.length;i++){
  if(!W[i]||!Number.isFinite(first[i])){R[i]=0;continue}
  const r=(Z[i]-first[i])/TUKEY;
  R[i]=Math.abs(r)<1?(1-r*r)**2:0;
 }
 // Signed curvature from headings 10 m either side; superelevation toward the inside.
 const E=new Float64Array(rows.length),ONES=new Float64Array(rows.length).fill(1);
 const heading=i=>{
  const j=Math.min(rows.length-1,i+1),k=Math.max(0,i-1);
  return Math.atan2(rows[j][2]-rows[k][2],rows[j][1]-rows[k][1]);
 };
 const H=rows.map((r,i)=>heading(i));
 for(let i=0;i<rows.length;i++){
  const j=Math.min(rows.length-1,i+5),k=Math.max(0,i-5),ds=S[j]-S[k];
  if(ds<4){E[i]=0;continue}
  let turn=H[j]-H[k];turn=Math.atan2(Math.sin(turn),Math.cos(turn));
  const curvature=turn/ds,radius=1/Math.max(Math.abs(curvature),1e-6);
  E[i]=Math.sign(curvature)*Math.min(.06,.5*designSpeed*designSpeed/(127*radius));
 }
 const smoothE=new Float64Array(rows.length);
 for(let i=0;i<rows.length;i++)smoothE[i]=gaussianLinear(S,E,ONES,S[i]);
 // Unit tangent along increasing station, so the side of the road and the
 // banking stay continuous around the sharp corners of simplified map lines.
 const TX=new Float64Array(rows.length),TY=new Float64Array(rows.length);
 for(let i=0;i<rows.length;i++){TX[i]=Math.cos(H[i]);TY[i]=Math.sin(H[i])}
 return {S,Z,W:R,E:smoothE,TX,TY,ONES};
}

// Height, superelevation and unit tangent of a chain's profile at station s.
function profileAt(profile,s){
 const tx=gaussianLinear(profile.S,profile.TX,profile.ONES,s),ty=gaussianLinear(profile.S,profile.TY,profile.ONES,s),l=Math.hypot(tx,ty)||1;
 return [gaussianLinear(profile.S,profile.Z,profile.W,s),gaussianLinear(profile.S,profile.E,profile.ONES,s),tx/l,ty/l];
}

// Design speed for superelevation, km/h. Low-speed streets keep a normal crown
// (AASHTO Green Book: superelevation is optional on low-speed urban streets), so 0.
function designSpeed(cls){return ['motorway','trunk'].includes(cls)?90:['primary','secondary'].includes(cls)?60:0}

export async function streamedTerrain(X,Y,tile,centerDEM,widths,readDEM,measured=null,readVector=null){
 const legacy=await mosaic(X,Y,centerDEM,readDEM);
 const mx=Math.floor(X/4),my=Math.floor(Y/4),parent=2**VECTOR_ZOOM;
 const lat=Math.atan(Math.sinh(Math.PI*(1-2*(my+.5)/parent)));
 const scale=EARTH_METRES/65536*Math.cos(lat);
 const summary={version:2,conditioned:0,structureProtected:0,bench:{roads:0,vertices:0,neighbours:0}};
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
 const toWorld=(x,y)=>[4*mx+x/scale,4*my+y/scale];
 const sample=(x,y)=>{const [wx,wy]=toWorld(x,y);return raw(wx,wy)};
 // This tile's box in parent metres.
 const tx0=(X-4*mx)*scale,ty0=(Y-4*my)*scale,tx1=tx0+scale,ty1=ty0+scale;
 const inside=(x,y)=>x>=tx0-SAMPLE_APRON&&x<=tx1+SAMPLE_APRON&&y>=ty0-SAMPLE_APRON&&y<=ty1+SAMPLE_APRON;

 // Road pieces: this parent clipped to itself; a neighbour parent is read only
 // when a road of this parent runs into it near this tile (its buffer shows it).
 const own=tileLines(tile,widths,0,0,scale,true);
 const unclipped=tileLines(tile,widths,0,0,scale,false);
 const size=4*scale,need=SAMPLE_APRON+WINDOW;
 const neighbours=[];
 for(let dy=-1;dy<=1;dy++)for(let dx=-1;dx<=1;dx++){
  if(!dx&&!dy)continue;
  const nx0=dx*size,ny0=dy*size,nx1=nx0+size,ny1=ny0+size;
  // Region of the neighbour that this tile's samples can reach.
  const rx0=Math.max(nx0,tx0-need),ry0=Math.max(ny0,ty0-need),rx1=Math.min(nx1,tx1+need),ry1=Math.min(ny1,ty1+need);
  if(rx0>=rx1||ry0>=ry1)continue;
  const crosses=unclipped.some(l=>!l.protectedRoad&&l.pts.some(([x,y])=>x>=rx0&&x<=rx1&&y>=ry0&&y<=ry1));
  if(crosses)neighbours.push([dx,dy]);
 }
 let pieces=own;
 if(readVector&&neighbours.length){
  for(const [dx,dy] of neighbours){
   const nx=mx+dx,ny=my+dy;
   if(ny<0||ny>=parent)continue;
   try{
    const other=await readVector((nx+parent)%parent,ny);
    pieces=pieces.concat(tileLines(other,widths,dx*size,dy*size,scale,true));
    summary.bench.neighbours++;
   }catch{}
  }
 }
 const benchPieces=pieces.filter(p=>!p.protectedRoad);
 const structures=pieces.filter(p=>p.protectedRoad);

 // Chains near this tile and their profiles.
 const near=(pts,margin)=>pts.some(([x,y])=>x>=tx0-margin&&x<=tx1+margin&&y>=ty0-margin&&y<=ty1+margin);
 const chains=chainPieces(benchPieces).filter(parts=>parts.some(p=>near(p.piece.pts,BATTER_REACH+40)));
 const roads=new Map(); // bin -> [segment]
 const addSegment=(seg)=>{
  const r=seg.reach;
  for(let x=Math.floor((Math.min(seg.ax,seg.bx)-r)/BIN);x<=Math.floor((Math.max(seg.ax,seg.bx)+r)/BIN);x++)
   for(let y=Math.floor((Math.min(seg.ay,seg.by)-r)/BIN);y<=Math.floor((Math.max(seg.ay,seg.by)+r)/BIN);y++){
    const k=x+','+y;if(!roads.has(k))roads.set(k,[]);roads.get(k).push(seg);
   }
 };
 const cell=scale/FINE;
 chains.forEach((parts,id)=>{
  const cls=parts[0].piece.cls,width=Math.max(...parts.map(p=>p.piece.width));
  const profile=chainProfile(parts,sample,inside,designSpeed(cls));
  const half=width/2,bench=half+Math.max(1.5,1.2*cell);
  for(const part of parts){
   const {piece,s,length,reversed,offset}=part;
   for(let k=1;k<piece.pts.length;k++){
    const a=piece.pts[k-1],b=piece.pts[k],l=s[k]-s[k-1];
    if(l<.01)continue;
    const sa=offset+(reversed?length-s[k-1]:s[k-1]),sb=offset+(reversed?length-s[k]:s[k]);
    addSegment({chain:id,profile,ax:a[0],ay:a[1],bx:b[0],by:b[1],l,sa,sb,bench,reach:bench+BATTER_REACH});
   }
  }
 });
 for(const piece of structures){
  for(let k=1;k<piece.pts.length;k++){
   const a=piece.pts[k-1],b=piece.pts[k],l=Math.hypot(b[0]-a[0],b[1]-a[1]);
   if(l<.01)continue;
   addSegment({structure:true,elevated:piece.elevatedRoad,ax:a[0],ay:a[1],bx:b[0],by:b[1],l,half:piece.width/2,reach:piece.width/2+22});
  }
 }
 summary.bench.roads=chains.length;

 // Coarse ground: the source surface, with bridge decks kept from the legacy
 // DSM-like Terrarium surface under mapped elevated structures.
 const coarse=[];
 for(let j=0;j<=GRID;j++){
  for(let i=0;i<=GRID;i++){
   const wx=X+i/GRID,wy=Y+j/GRID,x=(wx-4*mx)*scale,y=(wy-4*my)*scale;
   const original=raw(wx,wy,true);
   let deck=1,protection=1;
   for(const sgm of roads.get(Math.floor(x/BIN)+','+Math.floor(y/BIN))??[]){
    if(!sgm.structure)continue;
    const d=segmentDistance(sgm,x,y).d;
    if(d>=sgm.reach)continue;
    const f=fade(sgm.half+10,sgm.reach,d);
    protection=Math.min(protection,f);
    if(sgm.elevated)deck=Math.min(deck,f);
   }
   if(protection<1)summary.structureProtected++;
   const base=measured&&deck<1?original+(legacy(wx,wy)-original)*(1-deck):original;
   coarse.push(Math.round(base*100)/100);
  }
 }
 // Fine grid: exact midpoints of the coarse triangles (Godot PlaneMesh uses the
 // NE/SW diagonal), then road benches.
 const W=FINE+1,h=new Array(W*W);
 for(let j=0;j<W;j++)for(let i=0;i<W;i++){
  const k=(j>>1)*(GRID+1)+(i>>1);
  h[j*W+i]=i%2&&j%2?(coarse[k+1]+coarse[k+GRID+1])/2:i%2?(coarse[k]+coarse[k+1])/2:j%2?(coarse[k]+coarse[k+GRID+1])/2:coarse[k];
 }
 for(let j=0;j<W;j++){
  for(let i=0;i<W;i++){
   const x=tx0+i*cell,y=ty0+j*cell,base=h[j*W+i];
   const nearby=roads.get(Math.floor(x/BIN)+','+Math.floor(y/BIN));
   if(!nearby)continue;
   // Nearest projection per chain, and structure protection.
   const best=new Map();let protection=1;
   for(const sgm of nearby){
    const q=segmentDistance(sgm,x,y);
    if(q.d>=sgm.reach)continue;
    if(sgm.structure){protection=Math.min(protection,fade(sgm.half+10,sgm.reach,q.d));continue}
    const prior=best.get(sgm.chain);
    if(!prior||q.d<prior.q.d)best.set(sgm.chain,{sgm,q});
   }
   if(!best.size||protection<=0)continue;
   let flatW=0,flatZ=0,lo=-Infinity,hi=Infinity,nearest=null,fadeOut=1;
   for(const {sgm,q} of best.values()){
    const station=sgm.sa+(sgm.sb-sgm.sa)*q.t;
    const [z,e,tx,ty]=profileAt(sgm.profile,station);
    if(!Number.isFinite(z))continue;
    // Lateral offset from the centreline, positive on the side the chain turns
    // toward when e > 0; superelevation lowers the inside of the curve.
    const lateral=(x-q.x)*-ty+(y-q.y)*tx;
    if(q.d<=sgm.bench){
     const w=Math.exp(-.5*(q.d/3)**2);
     flatW+=w;flatZ+=w*(z-e*lateral);
    }else{
     const edge=z-e*sgm.bench*Math.sign(lateral);
     const over=q.d-sgm.bench;
     const l=edge-FILL*over,u=edge+CUT*over;
     if(!nearest||q.d<nearest.d)nearest={d:q.d,l,u};
     lo=Math.max(lo,l);hi=Math.min(hi,u);
     fadeOut=Math.min(fadeOut,1-fade(sgm.bench+BATTER_REACH-7,sgm.bench+BATTER_REACH,q.d));
    }
   }
   let target;
   if(flatW>0)target=flatZ/flatW;
   else if(nearest){
    if(lo>hi){lo=nearest.l;hi=nearest.u}
    target=base+(limit(base,lo,hi)-base)*fadeOut;
   }else continue;
   const adjusted=base+(target-base)*protection;
   if(Math.abs(adjusted-base)>.005){
    h[j*W+i]=Math.round(adjusted*100)/100;
    summary.bench.vertices++;
   }
  }
 }
 summary.conditioned=summary.bench.vertices;
 return {h,grid:FINE,road_grade:summary};
}

// Distance from (x,y) to a segment, its parameter, and which side it lies on.
function segmentDistance(sgm,x,y){
 const dx=sgm.bx-sgm.ax,dy=sgm.by-sgm.ay,l2=dx*dx+dy*dy;
 const t=limit(((x-sgm.ax)*dx+(y-sgm.ay)*dy)/l2,0,1);
 const qx=sgm.ax+t*dx,qy=sgm.ay+t*dy;
 return {d:Math.hypot(x-qx,y-qy),t,x:qx,y:qy};
}
