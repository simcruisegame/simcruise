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
function chainProfile(parts,sample,inside,designSpeed,bridgeAt=[false,false]){
 const rows=[];
 const chainTotal=parts.reduce((sum,p)=>sum+p.length,0);
 for(const part of parts){
  const {piece,s,length,reversed,offset}=part;
  let seg=0;
  // Every 2 m, and the piece's exact end where the chain meets a bridge deck:
  // without it the chain stopped up to 2 m short of the deck, leaving a gap
  // the tyres fell into. Other chain ends keep the 2 m stations, so a road
  // without a bridge is fitted exactly as before.
  const stations=[];
  for(let l=0;l<=length+1e-9;l+=STEP)stations.push(l);
  const endStation=offset+(reversed?0:length);
  const atDeck=(bridgeAt[0]&&endStation<1e-6)||(bridgeAt[1]&&Math.abs(endStation-chainTotal)<1e-6);
  if(atDeck&&length-stations[stations.length-1]>.05)stations.push(length);
  for(const l of stations){
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
 const profile={S,Z,W:R,E:smoothE,TX,TY,ONES,fix:[],X:rows.map(r=>r[1]),Y:rows.map(r=>r[2])};
 // An approach that ends at a bridge usually climbs an embankment, and a
 // one-sided wide fit undershoots that curve. Measure the end with a short
 // uniform fit over the last 25 m and blend the profile onto it over 30 m, so
 // the bench meets the deck (bridgeDecks reads the same corrected value).
 const total=S.length?S[S.length-1]:0;
 for(const [atEnd,end] of [[bridgeAt[0],0],[bridgeAt[1],total]]){
  if(!atEnd||S.length<8)continue;
  let n=0,sx=0,sy=0,sxx=0,sxy=0;
  for(let i=0;i<S.length;i++){
   const d=Math.abs(S[i]-end);
   if(d>25||!(W[i]>0))continue;
   const x=S[i]-end;n++;sx+=x;sy+=Z[i];sxx+=x*x;sxy+=x*Z[i];
  }
  if(n<5)continue;
  const det=n*sxx-sx*sx;
  const short=Math.abs(det)>1e-9?(sy*sxx-sx*sxy)/det:sy/n;
  const wide=gaussianLinear(S,Z,R,end);
  if(Number.isFinite(short)&&Number.isFinite(wide))profile.fix.push({end,delta:short-wide});
 }
 return profile;
}
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
 for(const {cls,width,widthAt,profile} of chains){
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
  for(const pts of rows)out.push({cls,w:Math.round(width*100)/100,pts});
 }
 return out;
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
  const cls=parts[0].piece.cls,width=Math.max(...parts.map(p=>p.piece.width));
  const ends=[parts[0].reversed?parts[0].piece.pts[parts[0].piece.pts.length-1]:parts[0].piece.pts[0],
   parts[parts.length-1].reversed?parts[parts.length-1].piece.pts[0]:parts[parts.length-1].piece.pts[parts[parts.length-1].piece.pts.length-1]];
  const bridgeAt=ends.map(a=>bridgeEnds.some(b=>Math.hypot(a[0]-b[0],a[1]-b[1])<=3));
  const profile=chainProfile(parts,sample,inside,designSpeed(cls),bridgeAt);
  const widthAt=widthAlong(parts);
  surfaceChains.push({cls,width,widthAt,profile});
  banked.push({id,profile,widthAt,banked:designSpeed(cls)>0});
  const total=parts.reduce((sum,p)=>sum+p.length,0);
  // Chain ends: a bridge deck starts at the bench height where its approach ends.
  for(const [part,atStart] of [[parts[0],true],[parts[parts.length-1],false]]){
   const pts=part.piece.pts;
   // Station 0 lies at the start of parts[0] in chain order, the total at the end of the last part.
   const end=atStart?(part.reversed?pts[pts.length-1]:pts[0]):(part.reversed?pts[0]:pts[pts.length-1]);
   terminals.push({x:end[0],y:end[1],profile,station:atStart?0:total,cls,name:part.piece.name});
  }
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
 // Before anything reads the approach profiles (benches, ribbons, deck ends).
 const reconciled=reconcileBridgeEnds(chainPieces(structures.filter(p=>p.bridge)).filter(parts=>parts.some(p=>near(p.piece.pts,SAMPLE_APRON))),terminals,scale);
 if(reconciled)summary.bench.bridgeEndsReconciled=reconciled;
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
 // A deck end beyond what every tile of the deck can fit alike is read from the
 // Terrarium elevation model at the end itself (bridgeDecks, END_SHARED), never
 // at a cut: reading it at the clipped ends put the Mackinac Bridge's mid-span
 // decks at lake level. Only an end where the loaded map data stops (a bridge
 // longer than the vector tiles followed) is unknown.
 const farTiles=new Map();
 const farGround=async(x,y)=>{
  const [wx,wy]=toWorld(x,y);
  const px=wx*32-.5,py=wy*32-.5,ix=Math.floor(px),iy=Math.floor(py),fx=px-ix,fy=py-iy;
  const at=async(ix,iy)=>{
   const tx=Math.floor(ix/SOURCE_SIZE),ty=Math.floor(iy/SOURCE_SIZE),key=tx+','+ty;
   if(!farTiles.has(key))farTiles.set(key,Promise.resolve().then(()=>readDEM((tx+2**DEM_ZOOM)%2**DEM_ZOOM,limit(ty,0,2**DEM_ZOOM-1))).then(b=>decode(b)).catch(()=>null));
   const im=await farTiles.get(key);
   if(!im||im.width!==SOURCE_SIZE||im.channels<3)return NaN;
   const i=((iy-ty*SOURCE_SIZE)*im.width+ix-tx*SOURCE_SIZE)*im.channels;
   return im.data[i]*256+im.data[i+1]+im.data[i+2]/256-32768;
  };
  const a=await at(ix,iy)*(1-fx)+await at(ix+1,iy)*fx;
  const b=await at(ix,iy+1)*(1-fx)+await at(ix+1,iy+1)*fx;
  return a*(1-fy)+b*fy;
 };
 const bridges=await bridgeDecks(structures.filter(p=>p.bridge),terminals,sample,inside,{tx0,ty0,scale},farGround,roads);
 summary.bench.bridges=bridges.length;
 return {h,grid:FINE,road_grade:summary,bridges,road_surface:roadSurface(surfaceChains,{tx0,ty0,scale})};
}

// Bridge decks. Each bridge chain runs straight in profile between the bench
// heights of the approaches it meets (a bare-earth DEM has no deck). An end is
// measured from the approach chain that ends there, else from any road bench
// passing through it (a junction at the abutment), else from the cache, else
// the ground; the game then fits the deck's last metres to the approach ribbon
// it actually built (BridgeDeck.gd). Connected bridge pieces share one profile,
// so a deck that changes class or turns sharply onto another (a ramp leaving a
// bridge) meets it at the same height, but each keeps its own row, class and
// width (before, a service bridge joined to a secondary one gave the secondary
// road a 4.6 m service deck). Rows: [class, name, points in tile units clipped
// to the tile, width, deck height per point, layer, oneway].
const BRIDGE_TURN=.5; // cos 60°: a sharper joint starts a new deck row
// Every tile a deck crosses must build the same deck, so an end's height may
// come from this tile's own fit (approach profile, bench, 3DEP ground) only
// when the end lies within END_SHARED of every z16 tile the deck crosses: each
// of them then fits that end from the same data. Otherwise every tile takes
// the end from farGround (the Terrarium elevation model at the end itself,
// world-anchored and readable anywhere), and the game fits the deck's last
// metres to the approach the tile holding that end built (BridgeDeck.gd).
// Before, an end out of reach came from heights earlier tiles had left in the
// cache, so whether a deck existed, and its height at a seam, depended on the
// order the tiles were fetched in (the Mass Pike viaduct in Allston MA: 6.58 m
// one way, a missing deck the other).
const END_SHARED=90;
// Where p lies within JOINT_M of a deck shape's centreline and more than
// JOINT_M from both its ends: {d, k, t} on segment k, or null.
const JOINT_M=3;
function onInterior(o,p){
 let best=null;
 for(let k=1;k<o.pts.length;k++){
  const ax=o.pts[k-1][0],ay=o.pts[k-1][1],dx=o.pts[k][0]-ax,dy=o.pts[k][1]-ay,l2=dx*dx+dy*dy;
  if(l2<1e-6)continue;
  const t=limit(((p[0]-ax)*dx+(p[1]-ay)*dy)/l2,0,1),d=Math.hypot(p[0]-ax-t*dx,p[1]-ay-t*dy);
  const at=o.s[k-1]+(o.s[k]-o.s[k-1])*t;
  if(d<=JOINT_M&&at>JOINT_M&&at<o.length-JOINT_M&&(!best||d<best.d))best={d,k,t};
 }
 return best;
}
function profileOn(o,h){return o.z[h.k-1]+(o.z[h.k]-o.z[h.k-1])*h.t}
// z16 tiles (as [i, j] in a parent's metres / scale) a polyline passes through
// or within `margin` metres of.
function tilesNear(pts,margin,scale){
 const out=new Map();
 const add=(x,y)=>{
  for(let i=Math.floor((x-margin)/scale);i<=Math.floor((x+margin)/scale);i++)
   for(let m=Math.floor((y-margin)/scale);m<=Math.floor((y+margin)/scale);m++)out.set(i+','+m,[i,m]);
 };
 if(pts.length===1)add(pts[0][0],pts[0][1]);
 for(let k=1;k<pts.length;k++){
  const n=Math.max(1,Math.ceil(Math.hypot(pts[k][0]-pts[k-1][0],pts[k][1]-pts[k-1][1])/2));
  for(let j=0;j<=n;j++)add(pts[k-1][0]+(pts[k][0]-pts[k-1][0])*j/n,pts[k-1][1]+(pts[k][1]-pts[k-1][1])*j/n);
 }
 return [...out.values()];
}
function withinTile([i,m],p,reach,scale){return p[0]>=i*scale-reach&&p[0]<=(i+1)*scale+reach&&p[1]>=m*scale-reach&&p[1]<=(m+1)*scale+reach}
// A chain end on a z14 edge is where the loaded map data stops, not the bridge.
function atVectorEdge(p,scale){const size=4*scale;return Math.abs(p[0]/size-Math.round(p[0]/size))*size<.5||Math.abs(p[1]/size-Math.round(p[1]/size))*size<.5}

// A bridge's two ends cannot be further apart in height than a deck can climb
// between them. Where the approaches' fitted end heights are (River Road, Stowe
// VT: 203.2 and 213.2 m, 21.7 m apart, a 46 % deck, because the 3DEP ground
// beside the east abutment stands 16 m above the road), the end whose approach
// is steeper over its last 20 m is the one the elevation model misled: its end
// fix is changed so that it meets the other end within DECK_MAX_GRADE, and its
// approach blends back to its own profile over FIX_SPAN as any end fix does.
// Every tile that holds the bridge, either approach's last FIX_SPAN or ground
// benched from it (RECONCILE_REACH) must read both ends from the same data
// (END_SHARED), or the bridge is left as measured; a road with no bridge end is
// never touched.
const DECK_MAX_GRADE=.08,RECONCILE_REACH=70;
function reconcileBridgeEnds(bridgeChains,terminals,scale){
 let changed=0;
 for(const parts of bridgeChains){
  const pts=[];
  for(const part of parts){const p=part.reversed?part.piece.pts.slice().reverse():part.piece.pts;for(const q of p)pts.push(q)}
  if(pts.length<2)continue;
  const ends=[pts[0],pts[pts.length-1]];
  if(ends.some(p=>atVectorEdge(p,scale)))continue;
  const s=arcLength(pts),length=s[s.length-1];
  if(length<2)continue;
  const meet=ends.map(p=>{
   let best=null;
   for(const t of terminals){const d=Math.hypot(t.x-p[0],t.y-p[1]);if(d<=3&&(!best||d<best.d))best={d,t}}
   return best?.t;
  });
  if(!meet[0]||!meet[1]||meet[0].profile===meet[1].profile)continue;
  const tiles=new Map();
  for(const t of [...tilesNear(pts,1,scale),...tilesNear([ends[0]],RECONCILE_REACH,scale),...tilesNear([ends[1]],RECONCILE_REACH,scale)])tiles.set(t.join(','),t);
  if(![...tiles.values()].every(t=>ends.every(p=>withinTile(t,p,END_SHARED,scale))))continue;
  const z=meet.map(t=>profileAtHeight(t.profile,t.station));
  if(!z.every(Number.isFinite))continue;
  const allowed=DECK_MAX_GRADE*length;
  if(Math.abs(z[0]-z[1])<=allowed+.01)continue;
  // Grade of each approach over its last 20 m.
  const steep=meet.map((t,k)=>{
   const inward=t.station<1e-6?20:-20;
   const far=profileAtHeight(t.profile,t.station+inward);
   return Number.isFinite(far)?Math.abs(z[k]-far)/20:Infinity;
  });
  const moved=steep[0]>steep[1]?0:1,keep=1-moved;
  const target=z[keep]+Math.sign(z[moved]-z[keep])*allowed;
  const t=meet[moved],need=target-z[moved];
  const fix=t.profile.fix.find(f=>Math.abs(f.end-t.station)<.01);
  if(fix)fix.delta+=need;
  else t.profile.fix.push({end:t.station,delta:need});
  changed++;
 }
 return changed;
}
async function bridgeDecks(pieces,terminals,sample,inside,{tx0,ty0,scale},farGround=null,roads=new Map()){
 const rows=[],items=[],shapes=[];
 const tilesCrossed=pts=>tilesNear(pts,1,scale);
 const atDataEdge=p=>atVectorEdge(p,scale);
 const benchAt=p=>{
  let best=null;
  for(const sgm of roads.get(Math.floor(p[0]/BIN)+','+Math.floor(p[1]/BIN))??[]){
   if(sgm.structure)continue;
   const q=segmentDistance(sgm,p[0],p[1]);
   if(q.d<=3.5&&(!best||q.d<best.q.d))best={sgm,q};
  }
  if(!best)return NaN;
  return profileAtHeight(best.sgm.profile,best.sgm.sa+(best.sgm.sb-best.sgm.sa)*best.q.t);
 };
 for(const parts of chainPieces(pieces)){
  const pts=[],owner=[];
  parts.forEach((part,i)=>{
   const p=part.reversed?part.piece.pts.slice().reverse():part.piece.pts;
   // Pieces clipped at a z14 edge by each side end a few centimetres apart; a
   // joint closer than 0.5 m is one point, or the deck gets a sideways sliver.
   for(const q of p)if(!pts.length||Math.hypot(q[0]-pts[pts.length-1][0],q[1]-pts[pts.length-1][1])>.5){pts.push(q);owner.push(i)}
  });
  if(pts.length<2)continue;
  // A point every DECK_STEP, so the deck can follow the at-grade roads it
  // overlaps (deckOnGrade); the inserted points belong to the part they lie on.
  {
   const dp=[pts[0]],dow=[owner[0]];
   for(let k=1;k<pts.length;k++){
    const n=Math.max(1,Math.ceil(Math.hypot(pts[k][0]-pts[k-1][0],pts[k][1]-pts[k-1][1])/DECK_STEP));
    for(let j=1;j<=n;j++){dp.push(j===n?pts[k]:[pts[k-1][0]+(pts[k][0]-pts[k-1][0])*j/n,pts[k-1][1]+(pts[k][1]-pts[k-1][1])*j/n]);dow.push(owner[k])}
   }
   pts.length=0;pts.push(...dp);owner.length=0;owner.push(...dow);
  }
  const s=arcLength(pts),length=s[s.length-1];
  if(length<2)continue;
  shapes.push({parts,pts,owner,s,length});
 }
 // The decks this tile draws, and every deck one of their ends lies on (a ramp
 // or side road whose bridge ends on another bridge): only these need heights,
 // so the others' ends cost no elevation reads.
 const drawn=shapes.filter(c=>crossesBox(c.pts,tx0,ty0,tx0+scale,ty0+scale));
 const wanted=new Set(drawn);
 for(const c of drawn)for(const e of [c.pts[0],c.pts[c.pts.length-1]])for(const o of shapes)if(o!==c&&onInterior(o,e))wanted.add(o);
 for(const c of shapes){
  if(!wanted.has(c))continue;
  const {pts,s,length}=c;
  const crossed=tilesCrossed(pts);
  const shared=p=>crossed.every(t=>withinTile(t,p,END_SHARED,scale));
  // An end is measured from what every tile of the deck reads alike (see
  // END_SHARED), never from heights earlier tiles left in the cache.
  const endHeight=async(p)=>{
   if(atDataEdge(p))return NaN;
   if(!shared(p)||!inside(p[0],p[1])){
    const z=farGround?await farGround(p[0],p[1]):NaN;
    return Number.isFinite(z)?Math.round(z*100)/100:NaN;
   }
   let best=null;
   for(const t of terminals){const d=Math.hypot(t.x-p[0],t.y-p[1]);if(d<=3&&(!best||d<best.d))best={d,t}}
   let z=best?profileAtHeight(best.t.profile,best.t.station):NaN;
   if(!Number.isFinite(z))z=benchAt(p);
   if(Number.isFinite(z))return Math.round(z*100)/100;
   return sample(p[0],p[1]);
  };
  let za=await endHeight(pts[0]),zb=await endHeight(pts[pts.length-1]);
  if(!Number.isFinite(za)&&!Number.isFinite(zb))continue;
  if(!Number.isFinite(za))za=zb;
  if(!Number.isFinite(zb))zb=za;
  c.z=s.map(l=>za+(zb-za)*l/length);
 }
 // A deck end on another deck's interior is a joint (a ramp leaving a bridge
 // where chainPieces linked the bridge's two halves instead): it takes that
 // deck's height there. Measured from the road or ground under the joint, the
 // ramp leaving Veterans Bridge, Chattanooga TN, started 4.8 m below the bridge.
 // Read from every deck's own straight profile first, so the order of the
 // chains does not matter.
 const joined=new Map();
 for(const c of drawn){
  if(!c.z)continue;
  const ends=[0,c.pts.length-1].map(k=>{
   for(const o of shapes){if(o===c||!o.z)continue;const h=onInterior(o,c.pts[k]);if(h)return profileOn(o,h)}
   return NaN;
  });
  if(ends.some(Number.isFinite))joined.set(c,ends);
 }
 for(const [c,[ja,jb]] of joined){
  const za=Number.isFinite(ja)?ja:c.z[0],zb=Number.isFinite(jb)?jb:c.z[c.z.length-1];
  c.z=c.s.map(l=>Math.round((za+(zb-za)*l/c.length)*100)/100);
 }
 for(const c of drawn)if(c.z)items.push(c);
 // Every deck's straight profile first, then each one's corrections from the
 // others' (deckOnGrade), so the order of the chains does not matter.
 const graded=items.map(item=>deckOnGrade(item,roads,inside,items));
 items.forEach((item,i)=>{item.z=graded[i]});
 for(const {parts,pts,owner,z} of items){
  // Rows split where the class or width changes or the chain turns by more
  // than 60°; the joint point belongs to both rows.
  const groups=[];let from=0;
  for(let j=1;j<pts.length-1;j++){
   if(owner[j+1]===owner[j])continue;
   // pts[j] is the joint: the last point of one part (the next part's first
   // point was merged into it).
   const A=parts[owner[j]].piece,B=parts[owner[j+1]].piece;
   const ux=pts[j][0]-pts[j-1][0],uy=pts[j][1]-pts[j-1][1],vx=pts[j+1][0]-pts[j][0],vy=pts[j+1][1]-pts[j][1];
   const turn=(ux*vx+uy*vy)/((Math.hypot(ux,uy)*Math.hypot(vx,vy))||1);
   if(A.cls!==B.cls||A.width!==B.width||turn<BRIDGE_TURN){groups.push([from,j,owner[j]]);from=j}
  }
  groups.push([from,pts.length-1,owner[pts.length-1]]);
  for(const [g0,g1,ownerIndex] of groups){
   const piece=parts[ownerIndex].piece;
   // Clip to this tile, interpolating the deck height at the cut.
   const out=[],heights=[],x1=tx0+scale,y1=ty0+scale;
   const flush=()=>{if(out.length>=2)rows.push([piece.cls,piece.name,out.slice(),piece.width,heights.slice(),piece.layer,piece.oneway?1:0]);out.length=0;heights.length=0};
   for(let k=g0+1;k<=g1;k++){
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
 }
 return rows;
}
// A deck that overlaps an at-grade road running beside it (a ramp leaving the
// bridge road, the other carriageway of a divided road that comes down to grade
// first) is at that road's level where they overlap: two drivable surfaces in
// one place cannot be 1-3 m apart, and the straight profile between the
// bridge's far ends missed them (Cambridge St, Allston MA: a westbound deck
// 1.7 m below the ramp and eastbound carriageway it shares a footprint with, a
// dead stop at 18 m/s). Only roads within GRADE_BAND of the deck and running
// within 45° of it count, so a road passing under the bridge never pulls the
// deck down. The correction is the junction join of RoadSurface.gd: each
// overlap's difference fades out along the deck over a length that grows with
// it (4 % grade, 15-60 m), and overlapping fades are averaged. Every input lies
// within 60 m of the point it moves, inside every tile's 150 m sample apron,
// so both tiles of a seam move a deck cut identically.
const DECK_STEP=4,GRADE_BAND=3,GRADE_RATE=.04,GRADE_FADE_MIN=15,GRADE_FADE_MAX=60;
// Whether a road's centreline, within CROSS_LOOK of `station` along it, passes
// from one side of the deck centreline `pts` to the other (more than 0.5 m
// each way, beside the deck's interior, not past its ends).
const CROSS_LOOK=40;
function crossesDeck(profile,station,pts){
 let left=false,right=false;
 for(let i=0;i<profile.S.length;i++){
  if(Math.abs(profile.S[i]-station)>CROSS_LOOK)continue;
  const x=profile.X[i],y=profile.Y[i];
  let best=null;
  for(let k=1;k<pts.length;k++){
   const ax=pts[k-1][0],ay=pts[k-1][1],dx=pts[k][0]-ax,dy=pts[k][1]-ay,l2=dx*dx+dy*dy;
   if(l2<1e-6)continue;
   const raw=((x-ax)*dx+(y-ay)*dy)/l2,t=limit(raw,0,1);
   const d=Math.hypot(x-ax-t*dx,y-ay-t*dy);
   if(!best||d<best.d)best={d,raw,k,side:dx*(y-ay)-dy*(x-ax)};
  }
  if(!best||(best.k===1&&best.raw<0)||(best.k===pts.length-1&&best.raw>1))continue;
  if(best.d<.5)continue;
  if(best.side>0)left=true;else right=true;
  if(left&&right)return true;
 }
 return false;
}
function deckOnGrade(item,roads,inside,items){
 const {pts,owner,parts,s,z}=item;
 const anchors=[],crossing=new Map();
 for(let i=0;i<pts.length;i++){
  const p=pts[i];if(!inside(p[0],p[1]))continue;
  const j=Math.min(i+1,pts.length-1),k=Math.max(i-1,0);
  const ux=pts[j][0]-pts[k][0],uy=pts[j][1]-pts[k][1],ul=Math.hypot(ux,uy);
  if(ul<.01)continue;
  const half=parts[owner[i]].piece.width/2;
  let best=null;
  for(const sgm of roads.get(Math.floor(p[0]/BIN)+','+Math.floor(p[1]/BIN))??[]){
   if(sgm.structure)continue;
   const q=segmentDistance(sgm,p[0],p[1]);
   if(q.d>half+sgm.half)continue;
   const station=sgm.sa+(sgm.sb-sgm.sa)*q.t;
   // Past the end of the road (the approach meeting the deck end) is not beside it.
   if((q.raw<0||q.raw>1)&&Math.min(station,sgm.total-station)<.01)continue;
   const align=Math.abs((ux*(sgm.bx-sgm.ax)+uy*(sgm.by-sgm.ay))/(ul*sgm.l));
   if(align<Math.SQRT1_2)continue;
   // A road whose centreline crosses the deck's near here passes under the
   // bridge at a shallow angle, it does not run beside it: the Mass Pike under
   // Cambridge St, Allston MA, crossed at 30° and pulled the deck down onto
   // itself, 0.1 m from its lanes instead of over them.
   const key=sgm.chain+':'+station.toFixed(2);
   if(!crossing.has(key))crossing.set(key,crossesDeck(sgm.profile,station,pts));
   if(crossing.get(key))continue;
   // The road's surface under the deck's centreline: its profile tilted by its
   // cross slope at that offset, as the ribbon is built.
   const [zc,e,tx,ty]=profileAt(sgm.profile,station);
   const zb=zc-e*((p[0]-q.x)*-ty+(p[1]-q.y)*tx);
   if(!Number.isFinite(zb)||Math.abs(zb-z[i])>=GRADE_BAND)continue;
   if(!best||q.d<best.d)best={d:q.d,zb};
  }
  if(best)anchors.push([s[i],best.zb-z[i]]);
 }
 if(!anchors.length)return z;
 const out=z.slice();
 for(let i=0;i<z.length;i++){
  let sum=0,sw=0,wmax=0;
  for(const [sa,delta] of anchors){
   const F=Math.min(Math.max(Math.abs(delta)/GRADE_RATE,GRADE_FADE_MIN),GRADE_FADE_MAX);
   const d=Math.abs(s[i]-sa);if(d>=F)continue;
   let w=1-d/F;w=w*w*(3-2*w);
   sum+=w*delta;sw+=w;wmax=Math.max(wmax,w);
  }
  if(sw>0)out[i]=z[i]+sum/sw*wmax;
 }
 return out;
}
function profileAtHeight(profile,s){return gaussianLinear(profile.S,profile.Z,profile.W,s)+fixAt(profile,s)}

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
