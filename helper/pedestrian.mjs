import {classifyRings} from '@mapbox/vector-tile';

const paved = new Set(['paved','asphalt','concrete','paving_stones','sett','cobblestone','bricks','concrete:plates']);
const widths = {footway:1.8,cycleway:2.7,pedestrian:3.5,sidewalk:1.8};
const same = (a,b)=>a[0]===b[0] && a[1]===b[1];
const round = n=>Math.round(n*100000)/100000;

function pathKind(p) {
 if(p.class!=='path' || p.brunnel || Number(p.layer??0)!==0 || (p.level!=null && Number(p.level)!==0))return null;
 if([p.indoor,p.covered,p.bridge,p.tunnel].some(v=>v!=null && v!==0 && v!=='no') || ['indoor','underground','overground'].includes(p.location))return null;
 const kind=p.footway==='sidewalk'?'sidewalk':p.subclass;
 if(!Object.hasOwn(widths,kind))return null;
 return paved.has(p.surface) || (kind==='sidewalk' && !p.surface)?kind:null;
}

function bounds(points) {
 const b=[Infinity,Infinity,-Infinity,-Infinity];
 for(const [x,y] of points){b[0]=Math.min(b[0],x);b[1]=Math.min(b[1],y);b[2]=Math.max(b[2],x);b[3]=Math.max(b[3],y)}
 return b;
}
const overlaps=(a,b)=>a[0]<=b[2] && a[2]>=b[0] && a[1]<=b[3] && a[3]>=b[1];
function covers(rings,x,y) {
 let inside=false;
 for(const ring of rings)for(let i=1;i<ring.length;i++){
  const [ax,ay]=ring[i-1],[bx,by]=ring[i];
  if((ay>y)!==(by>y) && x<ax+(bx-ax)*(y-ay)/(by-ay))inside=!inside;
 }
 return inside;
}
function outsideBuildings(a,b,obstacles) {
 const box=bounds([a,b]), near=obstacles.filter(p=>overlaps(p.box,box));
 if(!near.length)return [[a,b]];
 const dx=b[0]-a[0],dy=b[1]-a[1],cuts=[0,1],pieces=[];
 for(const {rings} of near)for(const ring of rings)for(let i=1;i<ring.length;i++){
  const c=ring[i-1],d=ring[i],ex=d[0]-c[0],ey=d[1]-c[1],cross=dx*ey-dy*ex;
  if(Math.abs(cross)<1e-14)continue;
  const qx=c[0]-a[0],qy=c[1]-a[1],t=(qx*ey-qy*ex)/cross,u=(qx*dy-qy*dx)/cross;
  if(t>0 && t<1 && u>=0 && u<=1)cuts.push(t);
 }
 cuts.sort((x,y)=>x-y);
 const at=t=>[round(a[0]+t*dx),round(a[1]+t*dy)];
 for(let i=1;i<cuts.length;i++){
  const lo=cuts[i-1],hi=cuts[i],mid=(lo+hi)/2;
  if(hi-lo>1e-7 && !near.some(({rings})=>covers(rings,a[0]+dx*mid,a[1]+dy*mid)))pieces.push([at(lo),at(hi)]);
 }
 return pieces;
}

// The tile's existing feature iterator and line clipper use its motor-road coordinate
// system. Keep walks in a separate list so they never become driving or grading routes.
export function pedestrianSurfaces(each,clip) {
 const candidates=[];
 each('transportation',(f,point)=>{
  const kind=pathKind(f.properties);
  if(!kind || f.type!==2)return;
  for(const line of f.loadGeometry()){
   const runs=line.slice(1).map((p,i)=>clip(point(line[i]),point(p))).filter(s=>s && !same(s[0],s[1]));
   if(runs.length)candidates.push({kind,runs});
  }
 });
 if(!candidates.length)return [];
 // MVTs may merge buildings on opposite sides of a z16 boundary into one feature.
 // Use each actual polygon, including neighboring owners and courtyard holes.
 const obstacles=[];
 each('building',(f,point)=>{
  if(f.type!==3)return;
  for(const polygon of classifyRings(f.loadGeometry())) {
   const outer=polygon[0].map(point),box=bounds(outer);
   if(overlaps(box,[0,0,1,1]))obstacles.push({box,rings:[outer,...polygon.slice(1).map(r=>r.map(point))]});
  }
 });
 const paths=[];
 for(const {kind,runs} of candidates){
  let run=[];
  for(const [start,end] of runs)for(const [a,b] of outsideBuildings(start,end,obstacles)){
   if(same(a,b))continue;
   if(!run.length || !same(run.at(-1),a)){
    run=[a];paths.push([kind,widths[kind],run]);
   }
   run.push(b);
  }
 }
 return paths;
}
