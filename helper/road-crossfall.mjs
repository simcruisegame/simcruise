import fs from 'node:fs';
import path from 'node:path';
import registry from './crossfall-roads.json' with {type:'json'};

const DAY=86400000;
export function roadControls(x,y,fingerprint,dir){
 const key=x+'-'+y;
 let record=registry[key];
 const external=path.join(dir,'road-controls-'+key+'.json');
 try{
  const stat=fs.statSync(external,{throwIfNoEntry:false});
  if(stat?.isFile()&&stat.size<125000)record=JSON.parse(fs.readFileSync(external));
 }catch{return null;}
 if(!record||record.mapFingerprint!==fingerprint||record.id?.join('-')!==key||!Array.isArray(record.sections)||record.sections.length>768||!/^[a-f0-9]{64}$/.test(record.roadSignature))return null;
 const age=Date.now()-Date.parse(record.prepared);
 return Number.isFinite(age)&&age>=-DAY&&age<365*DAY?record:null;
}

function solve(points,previous){
 const mat=Array.from({length:4},()=>new Float64Array(5));
 for(const [t,u,height] of points){
  const a=[1,t,u,Math.abs(u)];
  const res=previous?Math.abs(height-a.reduce((v,d,i)=>v+d*previous[i],0)):0;
  const w=res>.045?.045/res:1;
  for(let i=0;i<4;i++){
   for(let j=i;j<4;j++)mat[i][j]+=a[i]*a[j]*w;
   mat[i][4]+=height*w*a[i];
  }
 }
 for(let i=0;i<4;i++)for(let j=0;j<i;j++)mat[i][j]=mat[j][i];
 let min=Infinity,max=0;
 for(let i=0;i<4;i++){
  let pivot=i;
  for(let j=i+1;j<4;j++)if(Math.abs(mat[j][i])>Math.abs(mat[pivot][i]))pivot=j;
  [mat[i],mat[pivot]]=[mat[pivot],mat[i]];
  const k=mat[i][i],size=Math.abs(k);
  min=Math.min(min,size);max=Math.max(max,size);
  if(!Number.isFinite(k)||min<1e-8||max>min*600)return null;
  for(let j=i;j<5;j++)mat[i][j]/=k;
  for(let r=0;r<4;r++)if(r!==i){
   const f=mat[r][i];for(let j=i;j<5;j++)mat[r][j]-=f*mat[i][j];
  }
 }
 return mat.map(row=>row[4]);
}
function fitStrip(points,half){
 if(points.length<18||points.length>1400)return null;
 let left=0,right=0,front=0,rear=0;
 for(const [t,u] of points){if(u<-.46*half)left++;if(u>.46*half)right++;if(t<-1)rear++;if(t>1)front++;}
 if(Math.min(left,right)<4||Math.min(front,rear)<3)return null;
 let model=null;
 for(let iteration=0;iteration<5;iteration++){model=solve(points,model);if(!model)return null;}
 const [h,g,a,c]=model;
 const residual=points.map(([t,u,z])=>Math.abs(z-h-g*t-a*u-c*Math.abs(u))).sort((x,y)=>x-y);
 if(residual[Math.ceil(.8*(residual.length-1))]>.075)return null;
 return [h,g,a-c,a+c];
}
const round=(v,n)=>Number(v.toFixed(n));
export function measuredSections(study,control,patch){
 if(!control||!study.metadata||!study.side||!study.project||!study.points.length)return [];
 const side=study.side,center=[(patch[0]+.5)*side/4,(patch[1]+.5)*side/4],radius=side*.62;
 const bins=new Map(),key=(x,y)=>x*1024+y;
 let count=0;
 for(let n=0;n<study.points.length;n++){
  const pts=study.points[n],meta=study.metadata[n];
  if(!meta||meta.length*4!==pts.length)return [];
  for(let i=0;i<meta.length;i++){
   if(pts[i*4+3]!==2)continue;
   const id=meta[i]>>>16,x=pts[i*4],y=pts[i*4+1],z=pts[i*4+2];
   if(!id||x<0||y<0||x>side||y>side||!Number.isFinite(z))continue;
   if(++count>350000)return [];
   const k=key(Math.floor(x/12),Math.floor(y/12));
   const list=bins.get(k)??[];list.push(x,y,z,id);bins.set(k,list);
  }
 }
 const rows=[];
 for(const src of control.sections){
  if(src.length!==8)continue;
  const [index,way,s,x,y,tx,ty,half]=src;
  if(!Number.isInteger(index)||index<0||!/^\d{1,16}$/.test(way)||!(half>=2.25&&half<=5)||
     ![s,x,y,tx,ty].every(Number.isFinite)||Math.abs(Math.hypot(tx,ty)-1)>.001||Math.min(x,y,side-x,side-y)<17||
     Math.hypot(x-center[0],y-center[1])>radius)continue;
  const strips=new Map();
  for(let xx=Math.floor((x-10)/12);xx<=Math.floor((x+10)/12);xx++)for(let yy=Math.floor((y-10)/12);yy<=Math.floor((y+10)/12);yy++){
   const bin=bins.get(key(xx,yy));if(!bin)continue;
   for(let j=0;j<bin.length;j+=4){
    const dx=bin[j]-x,dy=bin[j+1]-y,t=tx*dx+ty*dy,u=-ty*dx+tx*dy;
    if(Math.abs(t)>6.6||Math.abs(u)>half)continue;
    const id=bin[j+3],points=strips.get(id)??[];points.push([t,u,bin[j+2]]);strips.set(id,points);
   }
  }
  const fits=[];
  for(const [id,points] of [...strips].sort((a,b)=>a[0]-b[0])){
   const fit=fitStrip(points,half);if(fit)fits.push([id,fit,points.length]);
  }
  if(fits.length<2)continue;
  const [id,a,na]=fits[0],[id2,b,nb]=fits[1];
  if(Math.abs(a[0]-b[0])>.12||Math.abs(a[1]-b[1])>.018||Math.max(Math.abs(a[2]-b[2]),Math.abs(a[3]-b[3]))>.015||
   Math.max(Math.abs(a[2]),Math.abs(a[3]),Math.abs(b[2]),Math.abs(b[3]))>.08)continue;
  rows.push([index,way,s,round(x/side,8),round(y/side,8),tx,ty,half,round(a[1],5),round(a[2],5),round(a[3],5),id,id2,na,nb]);
 }
 const runs=[];
 for(const row of rows){
  const prev=runs.at(-1)?.at(-1);
  if(!prev||row[0]!==prev[0]||row[1]!==prev[1]||row[2]-prev[2]!==6||row[11]!==prev[11]||row[12]!==prev[12]||
    Math.max(Math.abs(row[9]-prev[9]),Math.abs(row[10]-prev[10]))>.04)runs.push([]);
  runs.at(-1).push(row);
 }
 let used=0;
 return runs.filter(r=>r.length>=3).sort((a,b)=>b.length-a.length).filter(r=>(used+=r.length)<=128).slice(0,12);
}
export function storeSections(study,control,patch,dir,expires,write){
 const runs=measuredSections(study,control,patch);
 if(!control)return null;
 const prefix=path.join(dir,'road-crossfall-v1-'+control.id.join('-'));
 const result={version:1,id:control.id,patch,expires:Math.min(expires,Date.parse(control.prepared)+365*DAY),roadSignature:control.roadSignature,prepared:control.prepared,project:study.project??null,runs};
 // Empty completed districts invalidate previous results for this patch.
 write(prefix+'-'+patch.join('-')+'.json',result);
 if(runs.length)write(prefix+'.ready',1);
 return {runs:runs.length,sections:runs.reduce((n,r)=>n+r.length,0)};
}
