// Read a small projected window from a Sentinel COG overview, with the observed
// geotransform and resolution instead of assuming alignment across UTM zones.
import {GeoTIFF} from 'geotiff';
import proj4 from 'proj4';
const limit=384, day=86400000;
export async function ridgeBand(feature,key,x,y,read,budget) {
 const asset=feature.assets?.[key],p=feature.properties||{};
 const epsg=Number(p['proj:epsg']||p['proj:code']?.split(':')[1]);
 if(!asset||epsg<32601||epsg>32660||!Number.isInteger(epsg))return null;
 const projection=proj4('EPSG:4326','+proj=utm +zone='+(epsg-32600)+' +datum=WGS84 +units=m +no_defs');
 const optical=key==='red',maxRange=optical?485000:120000,maxCalls=optical?5:10,maxBytes=optical?760000:260000;
 const mem=new Map(),size=asset['file:size'],shape=asset['proj:shape'],g=asset['proj:transform'];
 if(!Number.isSafeInteger(size)||size<1024||!Array.isArray(shape)||!Array.isArray(g)||!(g[0]>0&&g[4]<0&&g[1]===0&&g[3]===0))return null;
 const parsed=new URL(asset.href);
 if(parsed.protocol!=='https:'||!parsed.hostname.endsWith('.amazonaws.com')||parsed.username||parsed.password||!/^S2[A-D]_[0-9A-Z_]+_L2A$/.test(feature.id))return null;
 // GeoTIFF can request neighboring compressed tiles concurrently. Collapse
 // adjacent byte ranges into one HTTP read while keeping the actual COG offsets.
 let pending=[],timer;
 function flush() {
  const work=pending.sort((a,b)=>a.lo-b.lo);pending=[];timer=null;
  const groups=[];
  for(const item of work){
   const last=groups.at(-1);
   if(last&&item.lo<=last.hi+2049&&Math.max(item.hi,last.hi)-last.lo<maxRange){
    last.hi=Math.max(last.hi,item.hi);last.items.push(item);
   }else groups.push({lo:item.lo,hi:item.hi,items:[item]});
  }
  for(const group of groups){
   const {lo,hi,items}=group,len=hi-lo+1;
   if(budget.bytes+len>maxBytes||budget.calls>=maxCalls){
    for(const item of items)item.reject(Error('District COG transfer budget'));continue;
   }
   budget.bytes+=len;budget.calls++;
   const id='ridge-band-'+feature.id+'-'+key+'-'+lo+'-'+hi;
   read(id,asset.href,180*day,[lo,hi],5500,len).then(bytes=>{
    for(const item of items){
     const pos=bytes.byteOffset+item.lo-lo;item.resolve(bytes.buffer.slice(pos,pos+item.hi-item.lo+1));
    }
   },error=>{for(const item of items)item.reject(error);});
  }
 }
 function readSlice({offset,length}){
  if(!Number.isSafeInteger(offset)||!Number.isSafeInteger(length)||offset<0||length<1||offset>=size)throw Error('Invalid satellite window');
  const head=offset+length<=32768,lo=head?0:offset,hi=Math.min(size-1,head?32767:offset+length-1);
  if(hi-lo>=maxRange)throw Error('Oversized SCL range');
  const token=lo+'-'+hi;
  if(!mem.has(token))mem.set(token,new Promise((resolve,reject)=>{
   pending.push({lo,hi,resolve,reject});if(!timer)timer=setTimeout(flush,1);
  }));
  return mem.get(token).then(bytes=>bytes.slice(offset-lo,offset-lo+Math.min(length,size-offset)));
 }
 const source={fileSize:size,fetch:slices=>Promise.all(slices.map(readSlice))};
 const tif=await GeoTIFF.fromSource(source),n=await tif.getImageCount();
 if(n<1||n>10)return null;
 const corners=[];
 for(const a of [x,x+1])for(const b of [y,y+1])corners.push(projection.forward([a/2048*360-180,Math.atan(Math.sinh(Math.PI*(1-2*b/2048)))*180/Math.PI]));
 let chosen=null,difference=Infinity;
 for(let k=0;k<n;k++){
  const im=await tif.getImage(k),res=g[0]*shape[1]/im.getWidth();
  let error=Math.abs(Math.log(res/40));
  if(optical){
   if(res<39||res>165)continue;
   const counts=await im.getFileDirectory().loadValue('TileByteCounts');
   const nx=Math.ceil(im.getWidth()/im.getTileWidth()),ny=Math.ceil(im.getHeight()/im.getTileHeight());
   const xx=corners.map(p=>Math.floor((p[0]-g[2])/res/im.getTileWidth()));
   const yy=corners.map(p=>Math.floor((g[5]-p[1])/res/im.getTileHeight()));
   let total=0,largest=0;
   for(let iy=Math.max(0,Math.min(...yy));iy<Math.min(ny,Math.max(...yy)+1);iy++)
    for(let ix=Math.max(0,Math.min(...xx));ix<Math.min(nx,Math.max(...xx)+1);ix++){
     const length=counts[iy*nx+ix];total+=length;largest=Math.max(largest,length);
    }
   if(!total||largest>maxRange||total+budget.bytes>maxBytes)continue;
   error=total+res*700;
  }
  if(error<difference){chosen=im;difference=error;}
 }
 if(!chosen)return null;
 const dx=g[0]*shape[1]/chosen.getWidth(),dy=g[4]*shape[0]/chosen.getHeight();
 const xi=new Int32Array(limit*limit),yi=new Int32Array(limit*limit);
 let xmin=chosen.getWidth(),xmax=-1,ymin=chosen.getHeight(),ymax=-1,inside=0;
 for(let j=0;j<limit;j++){
  const lat=Math.atan(Math.sinh(Math.PI*(1-2*(y+(j+.5)/limit)/2048)))*180/Math.PI;
  const left=projection.forward([x/2048*360-180,lat]),right=projection.forward([(x+1)/2048*360-180,lat]);
  for(let i=0;i<limit;i++){
   const t=(i+.5)/limit,idx=j*limit+i;
   const col=Math.floor((left[0]+(right[0]-left[0])*t-g[2])/dx),row=Math.floor((left[1]+(right[1]-left[1])*t-g[5])/dy);
   xi[idx]=col;yi[idx]=row;
   if(col<0||col>=chosen.getWidth()||row<0||row>=chosen.getHeight()){xi[idx]=-1;continue;}
   xmin=Math.min(xmin,col);xmax=Math.max(xmax,col);ymin=Math.min(ymin,row);ymax=Math.max(ymax,row);inside++;
  }
 }
 if(inside<limit*limit*.15)return null;
 const band=await chosen.readRasters({samples:[0],window:[xmin,ymin,xmax+1,ymax+1],interleave:true,pool:null});
 const data=key==='scl'?new Uint8Array(limit*limit):new Uint16Array(limit*limit);
 for(let i=0;i<data.length;i++)if(xi[i]>=0)data[i]=band[(yi[i]-ymin)*(xmax-xmin+1)+xi[i]-xmin];
 return {data,spacing:dx};
}
