// Test mapped footprints against survey returns before admitting a roof surface.
// All fits use local metres. Absolute survey elevation is only used for checking
// local ground. Rendering uses the independently measured height above that ground.
const clamp=(x,a,b)=>Math.max(a,Math.min(b,x));
const q=(xs,f)=>{const s=[...xs].sort((a,b)=>a-b);return s[Math.min(s.length-1,Math.floor((s.length-1)*f))];};
function inRing(x,y,ring) {
 let inside=false;
 for(let i=0,j=ring.length-1;i<ring.length;j=i++){
  const [a,b]=ring[j],[c,d]=ring[i];
  if((b>y)!==(d>y)&&x<(c-a)*(y-b)/(d-b)+a)inside=!inside;
 }
 return inside;
}
const interior=(x,y,rings)=>inRing(x,y,rings[0])&&!rings.slice(1).some(r=>inRing(x,y,r));
function borders(x,y,rings){
 let best=Infinity;
 for(const ring of rings)for(let i=0;i<ring.length;i++){
  const a=ring[i],b=ring[(i+1)%ring.length],dx=b[0]-a[0],dy=b[1]-a[1];
  const f=clamp(((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy||1),0,1);
  best=Math.min(best,(x-a[0]-f*dx)**2+(y-a[1]-f*dy)**2);
 }
 return Math.sqrt(best);
}
function planeFit(rows,values,indices,weights){
 const n=rows[0].length,normal=Array.from({length:n},()=>new Float64Array(n+1));
 for(const i of indices){
  const f=rows[i],w=weights?weights[i]:1;
  for(let a=0;a<n;a++){
   for(let b=a;b<n;b++)normal[a][b]+=w*f[a]*f[b];
   normal[a][n]+=w*f[a]*values[i];
  }
 }
 for(let a=0;a<n;a++)for(let b=0;b<a;b++)normal[a][b]=normal[b][a];
 for(let j=0;j<n;j++){
  let k=j;for(let i=j+1;i<n;i++)if(Math.abs(normal[i][j])>Math.abs(normal[k][j]))k=i;
  [normal[j],normal[k]]=[normal[k],normal[j]];
  if(Math.abs(normal[j][j])<1e-6)return null;
  const p=normal[j][j];for(let i=j;i<=n;i++)normal[j][i]/=p;
  for(let i=0;i<n;i++)if(i!==j){const coeff=normal[i][j];for(let b=j;b<=n;b++)normal[i][b]-=coeff*normal[j][b];}
 }
 return normal.map(a=>a[n]);
}
const linear=(f,x,y)=>f[0]*x+f[1]*y+f[2];
const height=(coeff,x,y,planes)=>coeff[0]+coeff[1]*x+coeff[2]*y+(planes.length?coeff[3]*Math.min(...planes.map(t=>linear(t,x,y))):0);
function fit(samples,train,planes){
 const rows=samples.map(([x,y])=>planes.length?[1,x,y,Math.min(...planes.map(t=>linear(t,x,y)))]:[1,x,y]);
 const z=samples.map(p=>p[2]);
 let weights=null,coeff;
 for(let k=0;k<3;k++){
  coeff=planeFit(rows,z,train,weights);
  if(!coeff)return null;
  if(k<2){weights=new Float64Array(samples.length);for(const i of train)weights[i]=Math.min(1,.27/Math.max(.001,Math.abs(height(coeff,...samples[i].slice(0,2),planes)-z[i])));}
 }
 if(Math.hypot(coeff[1],coeff[2])>1.35||(planes.length&&(coeff[3]<.10||coeff[3]>1.65)))return null;
 const errors=train.map(i=>Math.abs(height(coeff,samples[i][0],samples[i][1],planes)-z[i]));
 return {coeff,planes,score:q(errors,.50)+.65*q(errors,.80)+(planes.length?.10:0)};
}
function candidates(ring){
 const axes=new Map();
 for(let i=0;i<ring.length;i++){
  const [a,b]=ring[i],[c,d]=ring[(i+1)%ring.length],ang=(Math.atan2(d-b,c-a)+Math.PI*2)%(Math.PI/2);
  let key=Math.round(ang*36/Math.PI)%18;
  const prev=axes.get(key)||0;axes.set(key,prev+Math.hypot(c-a,d-b));
 }
 const primary=[...axes].sort((a,b)=>b[1]-a[1]).slice(0,2);
 const faces=[[]];
 for(const [sector] of primary)for(const turn of [0,Math.PI/2]){
  const angle=sector*Math.PI/36+turn,c=Math.cos(angle),s=Math.sin(angle);
  const us=ring.map(p=>c*p[0]+s*p[1]),vs=ring.map(p=>-s*p[0]+c*p[1]);
  const l=Math.min(...us),r=Math.max(...us),b=Math.min(...vs),t=Math.max(...vs);
  for(const ratio of [.36,.50,.64]){
   const crest=l+(r-l)*ratio;
   faces.push([[c,s,-crest],[-c,-s,crest]]);
  }
  for(const endPitch of [.75,1.25])faces.push([[c,s,-l],[-c,-s,r],[-endPitch*s,endPitch*c,-endPitch*b],[endPitch*s,-endPitch*c,endPitch*t]]);
 }
 return faces;
}
function clip(ring,plane){
 const out=[];
 for(let j=0;j<ring.length;j++){
  const a=ring[j],b=ring[(j+1)%ring.length],sa=linear(plane,...a),sb=linear(plane,...b);
  if(sa>=-1e-7)out.push(a);
  if((sa< -1e-7)!==(sb< -1e-7)){const f=sa/(sa-sb);out.push([a[0]+f*(b[0]-a[0]),a[1]+f*(b[1]-a[1])]);}
 }
 return out;
}
function mesh(b,side,x0,y0,coeff,planes,ground,fitInfo){
 const ringPoints=b[0].flat().map(p=>[p[0]*side-x0,p[1]*side-y0]);
 const facets=[],vertices=[],index=new Map(),boundary=new Map();
 function vertex(p){
  const coords=[Math.round((p[0]+x0)/side*1e5),Math.round((p[1]+y0)/side*1e5)];
  const key=coords.join(',');
  if(!index.has(key)){index.set(key,vertices.length);vertices.push([...coords,Math.round((height(coeff,p[0],p[1],planes)-ground)*100)]);}
  return index.get(key);
 }
 for(let i=0;i<b[1].length;i+=3){
  const tri=[ringPoints[b[1][i]],ringPoints[b[1][i+1]],ringPoints[b[1][i+2]]];
  for(let k=0;k<Math.max(1,planes.length);k++){
   let part=tri;
   for(let j=0;j<planes.length&&part.length;j++)if(k!==j)part=clip(part,planes[j].map((v,t)=>v-planes[k][t]));
   for(let j=1;j+1<part.length;j++){
    let a=vertex(part[0]),v=vertex(part[j]),w=vertex(part[j+1]);
    if(a===v||v===w||w===a)continue;
    const A=vertices[a],B=vertices[v],C=vertices[w];
    const signed=(B[0]-A[0])*(C[1]-A[1])-(B[1]-A[1])*(C[0]-A[0]);
    if(Math.abs(signed)<1)continue;if(signed<0)[v,w]=[w,v];
    facets.push(a,v,w);
    for(const [s,t] of [[a,v],[v,w],[w,a]]){
     const key=Math.min(s,t)+','+Math.max(s,t);if(boundary.has(key))boundary.delete(key);else boundary.set(key,[s,t]);
    }
   }
  }
 }
 if(vertices.length<3||vertices.length>900||!facets.length||!boundary.size)return null;
 const eave=Math.min(...vertices.map(v=>v[2]))/100,top=Math.max(...vertices.map(v=>v[2]))/100;
 if(eave<1.6||top>125||top-eave>24)return null;
 return {footprint:b[0].map(r=>r.map(p=>p.map(v=>Math.round(v*1e5)))),v:vertices,f:facets,edge:[...boundary.values()].flat(),eave_m:eave,top_m:top,validation:fitInfo};
}
function makeIndex(clouds){
 const bins=new Map();
 for(const cloud of clouds)for(let i=0;i<cloud.length;i+=4){
  const p=[cloud[i],cloud[i+1],cloud[i+2],cloud[i+3]];
  const bx=Math.floor(p[0]/8),by=Math.floor(p[1]/8),key=bx+','+by;
  if(!bins.has(key))bins.set(key,[]);
  bins.get(key).push(p);
 }
 return bins;
}
export function fitRoofPatch(study){
 const {eligible,side,points,ground:sceneGround}=study;const profiles=[],refusals={};
 const reject=reason=>{refusals[reason]=(refusals[reason]||0)+1;};
 const bins=makeIndex(points);
 for(const {b,index} of eligible){
  const x0=b[4]*side,y0=b[5]*side;
  const rings=b[0].map(r=>r.map(p=>[p[0]*side-x0,p[1]*side-y0]));
  if(rings[0].length>75||b[0].length>3||rings[0].length<3){reject('complex outline');continue;}
  const xs=rings[0].map(p=>p[0]),ys=rings[0].map(p=>p[1]),xmin=Math.min(...xs),xmax=Math.max(...xs),ymin=Math.min(...ys),ymax=Math.max(...ys);
  if((xmax-xmin)*(ymax-ymin)>8000){reject('large footprint');continue;}
  const within=[],surround=[],borderSamples=[];
  for(let a=Math.floor((x0+xmin-20)/8);a<=Math.floor((x0+xmax+20)/8);a++)for(let d=Math.floor((y0+ymin-20)/8);d<=Math.floor((y0+ymax+20)/8);d++)for(const p of bins.get(a+','+d)||[]){
   const x=p[0]-x0,y=p[1]-y0;const inside=interior(x,y,rings),dist=borders(x,y,rings);
   if(p[3]===2){if(!inside&&dist<20)surround.push([x,y,p[2],dist]);}
   else if(inside&&dist>.48)within.push([x,y,p[2],p[3]]);
   if(!inside&&dist>.6&&dist<3.4)borderSamples.push([x,y,p[2]]);
  }
  if(surround.length<16){reject('ground returns');continue;}
  const groundCells=new Map();
  for(const [x,y,z,dist] of surround){
   const key=Math.floor(x/3)+','+Math.floor(y/3);
   if(!groundCells.has(key))groundCells.set(key,[]);
   groundCells.get(key).push([x,y,z,dist]);
  }
  const gSamples=Array.from(groundCells.values(),v=>v[Math.floor(v.length/2)]).sort((a,b)=>a[3]-b[3]).slice(0,130);
  if(gSamples.length<8){reject('ground coverage');continue;}
  const model=fit(gSamples,gSamples.map((_,i)=>i),[]);
  if(!model||model.score>1.1||Math.hypot(model.coeff[1],model.coeff[2])>.37){reject('ground relief');continue;}
  const ground=model.coeff[0];
  if(!Number.isFinite(ground)||Math.abs(ground-sceneGround)>43){reject('ground datum');continue;}
  const cells=new Map();
  for(const [x,y,z] of within){
   if(z<ground+1.8||z>ground+125)continue;
   const ix=Math.floor(x/1.2),iy=Math.floor(y/1.2),key=ix+','+iy;
   if(!cells.has(key))cells.set(key,{ix,iy,h:[],x:0,y:0});const c=cells.get(key);c.h.push(z);c.x+=x;c.y+=y;
  }
  const samples=[],train=[],test=[];
  for(const c of cells.values()){
   const i=samples.length;samples.push([c.x/c.h.length,c.y/c.h.length,q(c.h,.5)]);
   (((c.ix+2*c.iy)%5+5)%5===0?test:train).push(i);
  }
  if(train.length<24||test.length<9){reject('too few independent cells');continue;}
  let expected=0,observed=0;
  for(let ix=Math.floor(xmin/2);ix<=Math.ceil(xmax/2);ix++)for(let iy=Math.floor(ymin/2);iy<=Math.ceil(ymax/2);iy++){
   const x=(ix+.5)*2,y=(iy+.5)*2;
   if(!interior(x,y,rings)||borders(x,y,rings)<.55)continue;
   expected++;
   const k=Math.floor(x/1.2),l=Math.floor(y/1.2);
   if([[k,l],[k+1,l],[k-1,l],[k,l+1],[k,l-1]].some(([a,b])=>cells.has(a+','+b)))observed++;
  }
  if(expected<8||observed/expected<.79){reject('footprint support');continue;}
  let best=null;
  for(const planes of candidates(rings[0])){
   const model=fit(samples,train,planes);if(model&&(!best||model.score<best.score))best=model;
  }
  if(!best){reject('no stable planes');continue;}
  const err=test.map(i=>Math.abs(height(best.coeff,samples[i][0],samples[i][1],best.planes)-samples[i][2]));
  const med=q(err,.5),p90=q(err,.9),coherent=err.filter(v=>v<=.55).length/err.length;
  if(med>.26||p90>.79||coherent<.80){reject('withheld surface mismatch');continue;}
  // Perimeter measurements keep a planar patch in vegetation or on a
  // much larger neighbouring building from being assigned this outline.
  let edgeCount=0,edgeHits=0;
  const edgeDirections=new Set();
  for(let k=0;k<rings[0].length;k++){
   const a=rings[0][k],b=rings[0][(k+1)%rings[0].length],dx=b[0]-a[0],dy=b[1]-a[1],len=Math.hypot(dx,dy);
   const divisions=Math.floor(len/3);
   for(let j=0;j<divisions;j++){
    const t=(j+.5)/divisions,x=a[0]+t*dx,y=a[1]+t*dy;
    const lower=[];
    for(const p of borderSamples){
     if((p[0]-x)**2+(p[1]-y)**2>10.6)continue;
     lower.push(height(best.coeff,p[0],p[1],best.planes)-p[2]);
    }
    if(lower.length<2)continue;
    edgeCount++;
    if(q(lower,.5)>1.1){edgeHits++;edgeDirections.add(Math.round(Math.atan2(dy,dx)/(Math.PI/4))&7);}
   }
  }
  if(edgeHits<3||edgeCount<4||edgeHits/edgeCount<.46||edgeDirections.size<2){reject('unconfirmed eaves');continue;}
  const validation={edge_samples:edgeCount,edge_hits:edgeHits,ground_m:+ground.toFixed(2),held:test.length,median_m:+med.toFixed(3),p90_m:+p90.toFixed(3),support:+(observed/expected).toFixed(3),form:best.planes.length===4?'hip':best.planes.length===2?'gable':'plane'};
  const profile=mesh(b,side,x0,y0,best.coeff,best.planes,ground,validation);
  if(profile)profiles.push({...profile,index});else reject('unsupported mesh');
 }
 return {profiles,refusals};
}
