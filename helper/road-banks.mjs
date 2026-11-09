import {classifyRings} from '@mapbox/vector-tile';

const majors=new Set(['motorway','trunk','primary','secondary','tertiary']);
const limit=(v,lo,hi)=>Math.max(lo,Math.min(hi,v));
function smooth(lo,hi,v){const t=limit((v-lo)/(hi-lo),0,1);return t*t*(3-2*t)}
function bilinear(a,n,u,v){
 if(u<0||v<0||u>1||v>1)return NaN;
 const x=u*n,y=v*n,i=Math.min(n-1,Math.floor(x)),j=Math.min(n-1,Math.floor(y)),k=j*(n+1)+i;
 const ax=x-i,ay=y-j;
 return ((1-ax)*a[k]+ax*a[k+1])*(1-ay)+((1-ax)*a[k+n+1]+ax*a[k+n+2])*ay;
}
function eligible(p){
 return majors.has(p.class)&&!p.brunnel&&!Number(p.layer??0)&&!p.ramp&&!['unpaved','gravel','ground','dirt','sand','grass'].includes(p.surface);
}
function geometry(tile,x,y,side,widths){
 const roads=[],exclusions=[];
 for(const layer of ['transportation','building','water']){
  const features=tile.layers[layer];if(!features)continue;
  for(let i=0;i<features.length;i++){
   const f=features.feature(i),p=f.properties,scale=side*4/f.extent,x0=x%4*side,y0=y%4*side;
   const bounds=f.bbox(),near=(a,b,c,d)=>c>=-80&&a<=side+80&&d>=-80&&b<=side+80;
   if(!near(bounds[0]*scale-x0,bounds[1]*scale-y0,bounds[2]*scale-x0,bounds[3]*scale-y0))continue;
   if(layer==='transportation'&&f.type===2){
    const road=eligible(p),bridge=!!p.brunnel||Number(p.layer??0)!==0,w=(widths[p.class]??4)/2;
    for(const line of f.loadGeometry())for(let j=1;j<line.length;j++){
     const a=line[j-1],b=line[j],ax=a.x*scale-x0,ay=a.y*scale-y0,bx=b.x*scale-x0,by=b.y*scale-y0;
     if(!near(Math.min(ax,bx),Math.min(ay,by),Math.max(ax,bx),Math.max(ay,by)))continue;
     const dx=bx-ax,dy=by-ay,sq=dx*dx+dy*dy;if(sq<.01)continue;
     roads.push({ax,ay,dx,dy,sq,w,road,bridge});
    }
   }else if(f.type===3){
    for(const polygon of classifyRings(f.loadGeometry())){
     let xmin=Infinity,xmax=-Infinity,ymin=Infinity,ymax=-Infinity;
     for(const pt of polygon[0]){const u=pt.x*scale-x0,v=pt.y*scale-y0;xmin=Math.min(xmin,u);xmax=Math.max(xmax,u);ymin=Math.min(ymin,v);ymax=Math.max(ymax,v)}
     if(near(xmin,ymin,xmax,ymax))exclusions.push([xmin,ymin,xmax,ymax]);
    }
   }
  }
 }
 return {roads,exclusions};
}
// PlaneMesh uses the anti-diagonal in each cell. A finer triangulation of the
// original planar faces must leave wheel-contact heights exactly unchanged.
function triangleHeight(h,n,u,v){
 const x=limit(u,0,1)*n,y=limit(v,0,1)*n,i=Math.min(n-1,Math.floor(x)),j=Math.min(n-1,Math.floor(y));
 const k=j*(n+1)+i,fx=x-i,fy=y-j,a=h[k],b=h[k+1],c=h[k+n+1],d=h[k+n+2];
 return fx+fy<=1?a+(b-a)*fx+(c-a)*fy:d+(c-d)*(1-fx)+(b-d)*(1-fy);
}
export function applyRoadBanks(x,y,tile,widths,terrain,surface){
 // The bank detail grid is 128; a 64 terrain grid is refined onto it in the game,
 // a 128 grid (road benches) takes the deltas directly.
 if(![64,128].includes(terrain.grid)||!surface)return;
 const g=terrain.grid;
 const n=128,side=40075016.68557849/65536/Math.cosh(Math.PI*(1-2*(y+.5)/65536));
 const cell=side/n,guard=Math.SQRT2*cell+.75,original=terrain.h;
 const {roads,exclusions}=geometry(tile,x,y,side,widths);
 if(!roads.some(s=>s.road)||roads.length>500||exclusions.length>250)return;
 const deltas=[];
 for(let j=1;j<n;j++)for(let i=1;i<n;i++){
  const u=i/n,v=j/n,px=u*side,py=v*side;
  const edge=Math.min(px,py,side-px,side-py);
  if(edge<cell*1.1)continue;
  let nearest=Infinity,clear=Infinity,qx=0,qy=0;
  for(const s of roads){
   const t=limit(((px-s.ax)*s.dx+(py-s.ay)*s.dy)/s.sq,0,1),cx=s.ax+t*s.dx,cy=s.ay+t*s.dy;
   const d=Math.hypot(px-cx,py-cy);
   clear=Math.min(clear,d-s.w-(s.bridge?20:0));
   if(!s.road)continue;
   if(d<nearest){nearest=d;qx=cx;qy=cy}
  }
  if(nearest>=74||clear<=guard)continue;
  for(const [xmin,ymin,xmax,ymax] of exclusions){
   const d=Math.hypot(Math.max(0,xmin-px,px-xmax),Math.max(0,ymin-py,py-ymax));
   clear=Math.min(clear,d);
   if(clear<=guard)break;
  }
  if(clear<=guard)continue;
  const qedge=Math.min(qx,qy,side-qx,side-qy);
  const weight=smooth(guard,guard+3,clear)*smooth(cell,cell*3.0,edge)*smooth(0,8,qedge)*(1-smooth(43,74,nearest));
  if(weight<=0)continue;
  const roadH=bilinear(surface.z,128,qx/side,qy/side);
  const sample=surface.z[j*129+i],roadBase=triangleHeight(original,g,qx/side,qy/side);
  const k=j*(n+1)+i,residual=(sample-roadH)-(triangleHeight(original,g,u,v)-roadBase);
  if(!Number.isFinite(residual)||Math.abs(residual)>=6)continue;
  const delta=weight*residual;
  if(Math.abs(delta)<.025)continue;
  deltas.push(k,Math.round(delta*1000));
 }
 if(deltas.length)terrain.road_banks={source:surface.source,datum:'NAVD88',vertices:deltas.length/2,detailGrid:128,deltas};
}
