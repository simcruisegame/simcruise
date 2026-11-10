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

// A line is near a box when any of its segments enters it, not only when a
// vertex lies inside: a straight road can cross a whole tile (or its apron)
// between two vertices far outside it (Leakey TX, RR 337).
export function crossesBox(pts,x0,y0,x1,y1){
 for(let k=0;k<pts.length;k++){
  const [x,y]=pts[k];
  if(x>=x0&&x<=x1&&y>=y0&&y<=y1)return true;
  if(k&&clipSegment(pts[k-1][0],pts[k-1][1],x,y,x0,y0,x1,y1))return true;
 }
 return false;
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
  const width=(widths.of?widths.of(feature):widths[p.class])??8,f=size/feature.extent;
  for(const line of feature.loadGeometry()){
   let piece=[];
   const bridge=protectedRoad&&p.brunnel!=='tunnel'&&(PAVED.has(p.class)||['service','track'].includes(p.class))&&(p.brunnel==='bridge'||Number(p.layer??0)>0);
   const flush=()=>{if(piece.length>=2)out.push({pts:piece,width,protectedRoad,elevatedRoad,bridge,layer:Number(p.layer??0),oneway:Number(p.oneway??0),cls:p.class,name:p.name??''});piece=[]};
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
export function chainProfile(parts,sample,inside,designSpeed,bridgeAt=[false,false]){
 const rows=[];
 parts.forEach((part,index)=>{
  const {piece,s,length,reversed,offset}=part;
  let seg=0;
  const at=l=>{
   while(seg<s.length-2&&l>s[seg+1])seg++;
   const a=piece.pts[seg],b=piece.pts[seg+1],t=(l-s[seg])/Math.max(s[seg+1]-s[seg],1e-9);
   rows.push([offset+(reversed?length-l:l),a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t]);
  };
  let last=0;
  for(let l=0;l<=length+1e-9;l+=STEP){at(l);last=l}
  // A chain that ends at a bridge gets a station exactly at its end, so its
  // ribbon reaches the deck (stations every STEP stopped up to 2 m short, a
  // gap where the car dropped onto the bank under the abutment).
  const end=reversed?(index===0&&bridgeAt[0]):(index===parts.length-1&&bridgeAt[1]);
  if(end&&length-last>.05)at(length);
 });
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
 const profile={S,Z,W:R,E:smoothE,TX,TY,ONES,fix:[],X:rows.map(r=>r[1]),Y:rows.map(r=>r[2])};
 // An approach that ends at a bridge usually climbs an embankment, and a
 // one-sided wide fit undershoots that curve. Measure the end with a short
 // uniform fit over the last 25 m and blend the profile onto it over 30 m, so
 // the bench meets the deck (bridgeDecks reads the same corrected value).
 // A bare-earth DEM can also fall away at the abutment instead (the bank of
 // the river the bridge crosses, smoothed into the last 10-20 m of the road):
 // at Moab UT both US-191 approaches then dropped 1.4 m over their last 12 m
 // (11-12 % grades) into the Colorado River bridge, a kink that jolted a car at
 // 4-6 g and stopped it dead. A road does not dip into a bridge (it crosses
 // something lower), so the end is never put below the approach's own line,
 // fitted over BRIDGE_LINE_M before the last 25 m and carried to the end, and
 // samples of the last 25 m that lie below that line are not the road and get
 // no weight: the profile then runs on at the approach grade, with no dip and
 // no hump where a correction would meet it.
 const total=S.length?S[S.length-1]:0;
 const lineFit=(end,from,to)=>{
  let n=0,sx=0,sy=0,sxx=0,sxy=0;
  for(let i=0;i<S.length;i++){
   const d=Math.abs(S[i]-end);
   if(d<from||d>to||!(W[i]>0))continue;
   const x=S[i]-end;n++;sx+=x;sy+=Z[i];sxx+=x*x;sxy+=x*Z[i];
  }
  if(n<5)return null;
  const det=n*sxx-sx*sx;
  if(Math.abs(det)<=1e-9)return {a:sy/n,b:0};
  return {a:(sy*sxx-sx*sxy)/det,b:(n*sxy-sx*sy)/det};
 };
 for(const [atEnd,end] of [[bridgeAt[0],0],[bridgeAt[1],total]]){
  if(!atEnd||S.length<8)continue;
  const short=lineFit(end,0,25);
  if(!short)continue;
  const line=lineFit(end,25,25+BRIDGE_LINE_M);
  if(line&&line.a>short.a){
   for(let i=0;i<S.length;i++){
    const x=S[i]-end;
    if(Math.abs(x)<=25&&Z[i]<line.a+line.b*x-BRIDGE_DIP_M)R[i]=0;
   }
  }
  const top=line?Math.max(short.a,line.a):short.a;
  const wide=gaussianLinear(S,Z,R,end);
  if(Number.isFinite(wide))profile.fix.push({end,delta:top-wide});
 }
 return profile;
}
const BRIDGE_LINE_M=50,BRIDGE_DIP_M=.1;
const FIX_SPAN=30;
function fixAt(profile,s){
 let d=0;
 for(const f of profile.fix){const t=1-Math.min(Math.abs(s-f.end)/FIX_SPAN,1);d+=f.delta*t*t*(3-2*t)}
 return d;
}

// Height, superelevation and unit tangent of a chain's profile at station s.
function profileAt(profile,s){
 const fix=fixAt(profile,s);
 const tx=gaussianLinear(profile.S,profile.TX,profile.ONES,s),ty=gaussianLinear(profile.S,profile.TY,profile.ONES,s),l=Math.hypot(tx,ty)||1;
 return [gaussianLinear(profile.S,profile.Z,profile.W,s)+fix,gaussianLinear(profile.S,profile.E,profile.ONES,s),tx/l,ty/l];
}

// The engineered road surface for the game (RoadSurface.gd): every chain
// station inside this tile (plus a 6 m apron so neighbours meet) with the
// smooth profile height, superelevation and smoothed tangent. The game builds
// the drivable ribbon from these instead of from the terrain grid, whose
// 3.9 m triangles cannot follow a banked hairpin (checks/RoadProfile.gd).
// Rows: [x, y] in tile units (0..1 like roads), z m, e (cross slope, + lowers
// the side the left normal points to), tx, ty, w (paved width at the station, m).
// A chain can join roads of different widths (a one-way carriageway continuing
// as a two-way road); w changes linearly across a 20 m window centred on the
// joint (WIDTH_TAPER = 10 m on each side), and the row's own w is the widest
// part, as before.
const SURFACE_APRON=6,WIDTH_TAPER=10;
function widthAlong(parts){
 if(parts.every(p=>p.piece.width===parts[0].piece.width)){const w=parts[0].piece.width;return ()=>w}
 // Mean width of the parts over [s - WIDTH_TAPER, s + WIDTH_TAPER].
 return s=>{
  let sum=0,n=0;
  for(const p of parts){const o=Math.min(s+WIDTH_TAPER,p.offset+p.length)-Math.max(s-WIDTH_TAPER,p.offset);if(o>0){sum+=o*p.piece.width;n+=o}}
  return n>0?sum/n:parts[s<=0?0:parts.length-1].piece.width;
 };
}
function roadSurface(chains,{tx0,ty0,scale}){
 const out=[];
 for(const {cls,top,topM,width,widthAt,profile} of chains){
  if(LOOSE.has(cls))continue;
  const rows=[];let run=[];
  const flush=()=>{if(run.length>=2)rows.push(run);run=[]};
  for(let i=0;i<profile.S.length;i++){
   const x=profile.X[i],y=profile.Y[i];
   const inBox=x>=tx0-SURFACE_APRON&&x<=tx0+scale+SURFACE_APRON&&y>=ty0-SURFACE_APRON&&y<=ty0+scale+SURFACE_APRON;
   if(!inBox){flush();continue}
   const [z,e,tx,ty]=profileAt(profile,profile.S[i]);
   if(!Number.isFinite(z)){flush();continue}
   const r=(v,k)=>Math.round(v*k)/k;
   run.push([r((x-tx0)/scale,1e6),r((y-ty0)/scale,1e6),r(z,1e3),r(e,1e4),r(tx,1e4),r(ty,1e4),r(widthAt(profile.S[i]),100)]);
  }
  flush();
  for(const pts of rows)out.push(top!==cls?{cls,top,top_m:topM,w:Math.round(width*100)/100,pts}:{cls,top_m:topM,w:Math.round(width*100)/100,pts});
 }
 return out;
}

// A chain can join parts of different classes (a street that changes class at
// a junction). Its class (design speed, guard rails) is the class with the
// most length, ties to the first part; it was the first part's class. Its
// `top` class is the highest class of any part and `top_m` the metres of that
// class in it, and RoadSurface.gd builds and joins ribbons in that order (a
// chain with a short primary piece ranks below an all-primary road it
// crosses), so a through road is never snapped onto a lesser street that
// ends on it: at Jerome AZ, Hull Avenue (primary, 10 %
// grade) was in a chain of class "minor", ranked below Diaz Street, and was
// flattened onto that street's level end section across it, a 0.55 m step
// in the drive line (RoadDrive jolts of 7-9 g at station 171-172).
const CLASS_RANK={motorway:7,trunk:6,primary:5,secondary:4,tertiary:3,minor:2,residential:2,unclassified:2};
export function chainClass(parts){
 const length=new Map();
 for(const p of parts)length.set(p.piece.cls,(length.get(p.piece.cls)??0)+p.length);
 let best=parts[0].piece.cls;
 for(const [cls,l] of length)if(l>length.get(best))best=cls;
 return best;
}
export function chainTop(parts){
 let top=chainClass(parts);
 for(const p of parts)if((CLASS_RANK[p.piece.cls]??1)>(CLASS_RANK[top]??1))top=p.piece.cls;
 return top;
}

// Design speed for superelevation, km/h. Low-speed streets keep a normal crown
// (AASHTO Green Book: superelevation is optional on low-speed urban streets), so 0.
function designSpeed(cls){return ['motorway','trunk'].includes(cls)?90:['primary','secondary'].includes(cls)?60:0}

export async function streamedTerrain(X,Y,tile,centerDEM,widths,readDEM,measured=null,readVector=null,memo=null){
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

 const near=(pts,margin)=>crossesBox(pts,tx0-margin,ty0-margin,tx1+margin,ty1+margin);
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
  const crosses=unclipped.some(l=>(!l.protectedRoad||l.bridge)&&crossesBox(l.pts,rx0,ry0,rx1,ry1));
  if(crosses)neighbours.push([dx,dy]);
 }
 let pieces=own;
 const loaded=new Set(['0,0']);
 const load=async(dx,dy)=>{
  const key=dx+','+dy,nx=mx+dx,ny=my+dy;
  if(loaded.has(key)||!readVector||ny<0||ny>=parent)return;
  loaded.add(key);
  try{
   const other=await readVector((nx+parent)%parent,ny);
   pieces=pieces.concat(tileLines(other,widths,dx*size,dy*size,scale,true));
   summary.bench.neighbours++;
  }catch{}
 };
 for(const [dx,dy] of neighbours)await load(dx,dy);
 // Follow bridges across z14 edges until both ends of every nearby bridge are
 // inside loaded tiles, so every tile sees the whole deck it shares.
 for(let round=0;round<3;round++){
  let added=false;
  for(const parts of chainPieces(pieces.filter(p=>p.bridge))){
   if(!parts.some(p=>near(p.piece.pts,SAMPLE_APRON)))continue;
   for(const part of [parts[0],parts[parts.length-1]])for(const e of [part.piece.pts[0],part.piece.pts[part.piece.pts.length-1]]){
    const gx=e[0]/size,gy=e[1]/size;
    for(const [ddx,ddy] of [[-1,0],[1,0],[0,-1],[0,1]]){
     const onEdge=ddx?Math.abs(gx-Math.round(gx))*size<.5:Math.abs(gy-Math.round(gy))*size<.5;
     if(!onEdge)continue;
     const cx=Math.floor(gx+ddx*.01),cy=Math.floor(gy+ddy*.01);
     if(!loaded.has(cx+','+cy)){await load(cx,cy);added=true}
    }
   }
  }
  if(!added)break;
 }
 const benchPieces=pieces.filter(p=>!p.protectedRoad);
 const structures=pieces.filter(p=>p.protectedRoad);

 // Chains near this tile, or ending at a bridge near it, and their profiles.
 const bridgeEnds=[];
 for(const parts of chainPieces(structures.filter(p=>p.bridge))){
  if(!parts.some(p=>near(p.piece.pts,SAMPLE_APRON)))continue;
  for(const part of [parts[0],parts[parts.length-1]])bridgeEnds.push(part.piece.pts[0],part.piece.pts[part.piece.pts.length-1]);
 }
 const touchesBridge=parts=>parts.some(p=>[p.piece.pts[0],p.piece.pts[p.piece.pts.length-1]].some(a=>bridgeEnds.some(b=>Math.hypot(a[0]-b[0],a[1]-b[1])<=3)));
 const chains=chainPieces(benchPieces).filter(parts=>parts.some(p=>near(p.piece.pts,BATTER_REACH+40))||touchesBridge(parts));
 const roads=new Map(); // bin -> [segment]
 const addSegment=(seg)=>{
  const r=seg.reach;
  for(let x=Math.floor((Math.min(seg.ax,seg.bx)-r)/BIN);x<=Math.floor((Math.max(seg.ax,seg.bx)+r)/BIN);x++)
   for(let y=Math.floor((Math.min(seg.ay,seg.by)-r)/BIN);y<=Math.floor((Math.max(seg.ay,seg.by)+r)/BIN);y++){
    const k=x+','+y;if(!roads.has(k))roads.set(k,[]);roads.get(k).push(seg);
   }
 };
 const cell=scale/FINE,terminals=[],surfaceChains=[],banked=[];
 chains.forEach((parts,id)=>{
  const cls=chainClass(parts),width=Math.max(...parts.map(p=>p.piece.width));
  const ends=[parts[0].reversed?parts[0].piece.pts[parts[0].piece.pts.length-1]:parts[0].piece.pts[0],
   parts[parts.length-1].reversed?parts[parts.length-1].piece.pts[0]:parts[parts.length-1].piece.pts[parts[parts.length-1].piece.pts.length-1]];
  const bridgeAt=ends.map(a=>bridgeEnds.some(b=>Math.hypot(a[0]-b[0],a[1]-b[1])<=3));
  const profile=chainProfile(parts,sample,inside,designSpeed(cls),bridgeAt);
  const widthAt=widthAlong(parts);
  const top=chainTop(parts);
  surfaceChains.push({cls,top,topM:Math.round(parts.reduce((m,p)=>m+(p.piece.cls===top?p.length:0),0)),width,widthAt,profile});
  banked.push({id,profile,widthAt,banked:designSpeed(cls)>0});
  const total=parts.reduce((sum,p)=>sum+p.length,0);
  // Chain ends: a bridge deck starts at the bench height where its approach ends.
  for(const [part,atStart] of [[parts[0],true],[parts[parts.length-1],false]]){
   const pts=part.piece.pts;
   // Station 0 lies at the start of parts[0] in chain order, the total at the end of the last part.
   const end=atStart?(part.reversed?pts[pts.length-1]:pts[0]):(part.reversed?pts[0]:pts[pts.length-1]);
   terminals.push({x:end[0],y:end[1],profile,station:atStart?0:total,cls,name:part.piece.name});
  }
  // A deck can also start at a joint inside a chain: the two one-way
  // carriageways of a road chained tip to tip where they meet a two-way deck
  // (Jerome AZ, Hull Avenue: the deck end was 0.26 m above the ribbon through
  // that joint, measured from another road, and stopped a car from 11 m/s).
  // The joint's station on this chain measures the deck end there too.
  parts.forEach((part,k)=>{
   if(k===0)return;
   const pts=part.piece.pts,start=part.reversed?pts[pts.length-1]:pts[0];
   if(bridgeEnds.some(b=>Math.hypot(start[0]-b[0],start[1]-b[1])<=3))
    terminals.push({x:start[0],y:start[1],profile,station:part.offset,cls,name:part.piece.name});
  });
  for(const part of parts){
   const {piece,s,length,reversed,offset}=part;
   // Each part is benched to its own width (a chain can join a one-way carriageway to a wider two-way road).
   const bench=piece.width/2+Math.max(1.5,1.2*cell);
   for(let k=1;k<piece.pts.length;k++){
    const a=piece.pts[k-1],b=piece.pts[k],l=s[k]-s[k-1];
    if(l<.01)continue;
    const sa=offset+(reversed?length-s[k-1]:s[k-1]),sb=offset+(reversed?length-s[k]:s[k]);
    addSegment({chain:id,profile,ax:a[0],ay:a[1],bx:b[0],by:b[1],l,sa,sb,total,bench,half:piece.width/2,banked:designSpeed(cls)>0,reach:bench+BATTER_REACH});
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
 superelevationRunout(banked,roads);

 // Coarse ground: the source surface.
 const coarse=[];
 for(let j=0;j<=GRID;j++){
  for(let i=0;i<=GRID;i++){
   const wx=X+i/GRID,wy=Y+j/GRID,x=(wx-4*mx)*scale,y=(wy-4*my)*scale;
   const original=raw(wx,wy,true);
   let protection=1;
   for(const sgm of roads.get(Math.floor(x/BIN)+','+Math.floor(y/BIN))??[]){
    if(!sgm.structure)continue;
    const d=segmentDistance(sgm,x,y).d;
    if(d>=sgm.reach)continue;
    const f=fade(sgm.half+10,sgm.reach,d);
    protection=Math.min(protection,f);
   }
   if(protection<1)summary.structureProtected++;
   // Bridges are built as decks (bridgeDecks, BridgeDeck.gd), so the ground under
   // them is the bare earth: the creek, valley, road or rail yard below. (Before
   // decks existed, the legacy surface was kept here as a stand-in deck.)
   coarse.push(Math.round(original*100)/100);
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
   // A point that projects onto a road's interior within its bench is road,
   // even beside or under a structure (an approach up to its abutment, a road
   // passing under an overpass). Only ground past a road's end is protected.
   let onBench=false;
   for(const entry of best.values()){
    const {sgm,q}=entry;
    const station=sgm.sa+(sgm.sb-sgm.sa)*q.t;
    const pastEnd=(q.raw<0&&Math.min(station,sgm.total-station)<.01&&q.t===0)||(q.raw>1&&Math.min(station,sgm.total-station)<.01&&q.t===1);
    // The bench continues 5 m under a bridge's first metres, 5 cm below the
    // deck, so the last ground triangles before the deck are not half valley.
    entry.underDeck=pastEnd&&q.d<=5&&sgm.profile.fix.some(f=>Math.abs(f.end-station)<.01);
    if((!pastEnd||entry.underDeck)&&q.d<=sgm.bench)onBench=true;
   }
   if(onBench)protection=1;
   if(!best.size||protection<=0)continue;
   let flatW=0,flatZ=0,lo=-Infinity,hi=Infinity,nearest=null,fadeOut=1;
   for(const {sgm,q,underDeck} of best.values()){
    const station=sgm.sa+(sgm.sb-sgm.sa)*q.t;
    let [z,e,tx,ty]=profileAt(sgm.profile,station);
    if(underDeck)z-=.05;
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
 const worldKey=(x,y)=>{const [wx,wy]=toWorld(x,y);return Math.round(wx*2e4)+'-'+Math.round(wy*2e4)};
 // The z16 tile a point lies in (world tile units, floored).
 const ownerOf=(x,y)=>{const [wx,wy]=toWorld(x,y);return [Math.floor(Math.round(wx*2e4)/2e4),Math.floor(Math.round(wy*2e4)/2e4)]};
 const bridges=await bridgeDecks(structures.filter(p=>p.bridge),terminals,sample,inside,{tx0,ty0,scale},memo,worldKey,ownerOf,[X,Y]);
 summary.bench.bridges=bridges.length;
 return {h,grid:FINE,road_grade:summary,bridges,road_surface:roadSurface(surfaceChains,{tx0,ty0,scale})};
}

// Bridge decks. Each bridge chain runs straight in profile between the bench
// heights of the approaches it meets (a bare-earth DEM has no deck). An end
// without an approach in reach takes the ground there; a bridge with neither
// end known is left out. Rows: [class, name, points in tile units clipped to
// the tile, width, deck height per point, layer].
//
// Every tile a deck crosses must give it the same profile, or the deck steps at
// the tile seam (Moab UT, US-191 over the Colorado: 0.24 m, which stopped a
// car dead from 20 m/s). A tile measures an end well only when the end lies in
// it: its approach profile is fitted on this tile's own 3DEP read, and a
// neighbour's fit of the same approach near the edge of its read differed by
// 0.53 m. So each end belongs to the z16 tile that contains it (`owner`): that
// tile measures it (approach profile, else the ground) and records it in the
// cache (memo); every other tile reads the record and, when there is none yet,
// asks the owner to grade itself first (memo.resolve). The deck height then
// does not depend on which tile was built first.
export async function bridgeDecks(pieces,terminals,sample,inside,{tx0,ty0,scale},memo,worldKey,ownerOf=null,self=null){
 const rows=[];
 for(const parts of chainPieces(pieces)){
  const pts=[];
  for(const part of parts){
   const p=part.reversed?part.piece.pts.slice().reverse():part.piece.pts;
   // Pieces clipped at a z14 edge by each side end a few centimetres apart; a
   // joint closer than 0.5 m is one point, or the deck gets a sideways sliver.
   for(const q of p)if(!pts.length||Math.hypot(q[0]-pts[pts.length-1][0],q[1]-pts[pts.length-1][1])>.5)pts.push(q);
  }
  if(pts.length<2)continue;
  const s=arcLength(pts),length=s[s.length-1];
  if(length<2)continue;
  // An end is measured by the tile that owns it and remembered in the cache;
  // other tiles use that record (see above).
  const endHeight=async(p)=>{
   let best=null;
   for(const t of terminals){const d=Math.hypot(t.x-p[0],t.y-p[1]);if(d<=3&&(!best||d<best.d))best={d,t}}
   const key=worldKey(p[0],p[1]);
   const measure=()=>{
    if(best){
     const z=Math.round(profileAtHeight(best.t.profile,best.t.station)*100)/100;
     if(Number.isFinite(z)&&inside(p[0],p[1]))return z;
    }
    if(!inside(p[0],p[1]))return NaN;
    const g=sample(p[0],p[1]);
    return Number.isFinite(g)?Math.round(g*100)/100:NaN;
   };
   const owner=ownerOf?.(p[0],p[1]);
   if(!owner||!self||(owner[0]===self[0]&&owner[1]===self[1])){
    const z=measure();
    if(Number.isFinite(z)){if(owner)memo?.set(key,z);return z}
   }
   let known=memo?.get(key);
   if(!Number.isFinite(known)&&owner&&memo?.resolve){await memo.resolve(owner[0],owner[1]);known=memo.get(key)}
   if(Number.isFinite(known))return known;
   // The owner could not be graded (no data offline): measure here as before.
   return measure();
  };
  let za=await endHeight(pts[0]),zb=await endHeight(pts[pts.length-1]);
  if(!Number.isFinite(za)&&!Number.isFinite(zb))continue;
  if(!Number.isFinite(za))za=zb;
  if(!Number.isFinite(zb))zb=za;
  const z=s.map(l=>za+(zb-za)*l/length);
  // Clip to this tile, interpolating the deck height at the cut.
  const out=[],heights=[],x1=tx0+scale,y1=ty0+scale;
  const flush=()=>{if(out.length>=2){const p=parts[0].piece;rows.push([p.cls,p.name,out.slice(),p.width,heights.slice(),p.layer,p.oneway?1:0])}out.length=0;heights.length=0};
  for(let k=1;k<pts.length;k++){
   const c=clipSegment(pts[k-1][0],pts[k-1][1],pts[k][0],pts[k][1],tx0,ty0,x1,y1);
   if(!c){flush();continue}
   const L=Math.hypot(pts[k][0]-pts[k-1][0],pts[k][1]-pts[k-1][1])||1;
   const fa=Math.hypot(c[0]-pts[k-1][0],c[1]-pts[k-1][1])/L,fb=Math.hypot(c[2]-pts[k-1][0],c[3]-pts[k-1][1])/L;
   const za2=z[k-1]+(z[k]-z[k-1])*fa,zb2=z[k-1]+(z[k]-z[k-1])*fb;
   const u=v=>Math.round(v*1e5)/1e5;
   const a=[u((c[0]-tx0)/scale),u((c[1]-ty0)/scale)],b=[u((c[2]-tx0)/scale),u((c[3]-ty0)/scale)];
   const last=out[out.length-1];
   if(!last||last[0]!==a[0]||last[1]!==a[1]){flush();out.push(a);heights.push(Math.round(za2*100)/100)}
   out.push(b);heights.push(Math.round(zb2*100)/100);
   if(fb<.999999)flush();
  }
  flush();
 }
 return rows;
}
export function profileAtHeight(profile,s){return gaussianLinear(profile.S,profile.Z,profile.W,s)+fixAt(profile,s)}

// Superelevation run-out where banked roads meet. Two banked chains whose
// footprints touch (a couplet splitting from its approach, a ramp joining, two
// highways crossing at grade) are one paved area there, but each was banked
// for its own curve: where their cross slopes disagree, the higher edge of one
// stands above the other (Eureka CA, US-101: 0.35 m). As at an at-grade
// junction on a real road, the bank runs out: a station's superelevation is
// scaled down by how much its cross-slope vector differs from the other road's
// (fully at a difference of RUNOUT_SLOPE), fading back in over RUNOUT_M past
// the footprints' edges. Carriageways banked alike, side by side, keep theirs.
// Unbanked roads (designSpeed 0) meet banked ones by RoadSurface.gd join().
const RUNOUT_M=20,RUNOUT_SLOPE=.04,RUNOUT_SHOULDER=1.2;
function superelevationRunout(banked,roads){
 const factors=[];
 for(const {id,profile,widthAt,banked:own} of banked){
  const F=new Float64Array(profile.S.length).fill(1);
  factors.push(F);
  if(!own)continue;
  for(let i=0;i<profile.S.length;i++){
   const x=profile.X[i],y=profile.Y[i],e=profile.E[i];
   const nx=-profile.TY[i],ny=profile.TX[i];
   const near=new Map();
   for(const sgm of roads.get(Math.floor(x/BIN)+','+Math.floor(y/BIN))??[]){
    if(sgm.structure||!sgm.banked||sgm.chain===id)continue;
    const q=segmentDistance(sgm,x,y);
    const edge=q.d-widthAt(profile.S[i])/2-sgm.half-2*RUNOUT_SHOULDER;
    if(edge>=RUNOUT_M)continue;
    const prior=near.get(sgm.chain);
    if(!prior||edge<prior.edge)near.set(sgm.chain,{edge,sgm,q});
   }
   for(const {edge,sgm,q} of near.values()){
    const k=nearestStation(sgm.profile.S,sgm.sa+(sgm.sb-sgm.sa)*q.t);
    const e2=sgm.profile.E[k],n2x=-sgm.profile.TY[k],n2y=sgm.profile.TX[k];
    // Cross-slope vectors (the surface falls along e times the left normal).
    const conflict=Math.min(1,Math.hypot(e*nx-e2*n2x,e*ny-e2*n2y)/RUNOUT_SLOPE);
    const t=Math.max(0,Math.min(1,edge/RUNOUT_M)),reach=1-t*t*(3-2*t);
    F[i]=Math.min(F[i],1-conflict*reach);
   }
  }
 }
 banked.forEach(({profile},c)=>{for(let i=0;i<profile.E.length;i++)profile.E[i]*=factors[c][i]});
}
function nearestStation(S,s){
 let lo=0,hi=S.length-1;
 while(hi-lo>1){const m=(lo+hi)>>1;if(S[m]<s)lo=m;else hi=m}
 return Math.abs(S[lo]-s)<=Math.abs(S[hi]-s)?lo:hi;
}

// Distance from (x,y) to a segment, its parameter, and which side it lies on.
function segmentDistance(sgm,x,y){
 const dx=sgm.bx-sgm.ax,dy=sgm.by-sgm.ay,l2=dx*dx+dy*dy;
 const t=limit(((x-sgm.ax)*dx+(y-sgm.ay)*dy)/l2,0,1);
 const qx=sgm.ax+t*dx,qy=sgm.ay+t*dy;
 return {d:Math.hypot(x-qx,y-qy),t,raw:((x-sgm.ax)*dx+(y-sgm.ay)*dy)/l2,x:qx,y:qy};
}
