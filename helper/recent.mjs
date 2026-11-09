// Retrieve a small, recent, cloud-masked surface-colour image for one z14 neighbourhood.
import {GeoTIFF} from 'geotiff';
import {bareReflectance} from './bare-reflectance.mjs';
import proj4 from 'proj4';
import {encode} from 'fast-png';
const day=86400000;
const edge=128;
function geographic(x,y){
 return [x/16384*360-180,Math.atan(Math.sinh(Math.PI*(1-2*y/16384)))*180/Math.PI];
}
async function readWindow(scene,key,points,read){
 const asset=scene.assets[key],shape=asset['proj:shape'],transform=asset['proj:transform'];
 const xs=points.map(p=>Math.floor((p[0]-transform[2])/transform[0]));
 const ys=points.map(p=>Math.floor((p[1]-transform[5])/transform[4]));
 const box=[Math.min(...xs),Math.min(...ys),Math.max(...xs)+1,Math.max(...ys)+1];
 if(box[0]<0||box[1]<0||box[2]>shape[1]||box[3]>shape[0])return null;
 const url=asset.href,size=asset['file:size'];
 const memory=new Map();
 const source={fileSize:size,fetch:async slices=>Promise.all(slices.map(async({offset,length})=>{
  // A shared header covers the file directory. Fetch only specific compressed pixel blocks after it.
  const head=offset+length<=65536,lo=head?0:offset,hi=Math.min(size-1,head?65535:offset+length-1);
  if(hi-lo>4*1024*1024)throw Error('Oversized image block');
  const id='s2-'+scene.id+'-'+key+'-'+lo+'-'+hi;
  if(!memory.has(id))memory.set(id,read(id,url,180*day,[lo,hi]));
  const buf=await memory.get(id),start=buf.byteOffset+offset-lo;
  return buf.buffer.slice(start,start+Math.min(length,size-offset));
 }))};
 const tiff=await GeoTIFF.fromSource(source);
 const image=await tiff.getImage();
 const raster=await image.readRasters({window:box,interleave:true,pool:null});
 const bands=image.getSamplesPerPixel();
 return {get(i,band=0){return raster[((ys[i]-box[1])*(box[2]-box[0])+xs[i]-box[0])*bands+band]}};
}
export async function recent(mx,my,read){
 const [west,north]=geographic(mx,my),[east,south]=geographic(mx+1,my+1);
 const q=new URLSearchParams({collections:'sentinel-2-c1-l2a',bbox:[west,south,east,north].join(','),limit:'12',query:JSON.stringify({'eo:cloud_cover':{lt:40}})});
 const catalog=JSON.parse((await read('scene-list-'+mx+'-'+my,'https://earth-search.aws.element84.com/v1/search?'+q,day)).toString());
 const scenes=catalog.features.filter(f=>Date.now()-Date.parse(f.properties.datetime)<90*day).sort((a,b)=>b.properties.datetime.localeCompare(a.properties.datetime));
 const used=new Set();
 for(const scene of scenes){
  const date=scene.properties.datetime.slice(0,10);
  if(used.has(date)||used.size>=4)continue;
  const epsg=scene.properties['proj:epsg']||Number(scene.properties['proj:code']?.split(':')[1]);
  if(!(epsg>=32601&&epsg<=32660))continue;
  const project=proj4('EPSG:4326','+proj=utm +zone='+(epsg-32600)+' +datum=WGS84 +units=m +no_defs');
  const points=[];
  for(let y=0;y<edge;y++)for(let x=0;x<edge;x++)points.push(project.forward(geographic(mx+(x+.5)/edge,my+(y+.5)/edge)));
  const scl=await readWindow(scene,'scl',points,read);
  if(!scl)continue;
  used.add(date);
  const valid=points.map((_,i)=>[4,5,6,11].includes(scl.get(i)));
  const clear=valid.filter(Boolean).length/valid.length;
  if(clear<.74)continue;
  const rgb=await readWindow(scene,'visual',points,read);
  if(!rgb)continue;
  const data=new Uint8Array(edge*edge*4);
  const classes=new Uint8Array(edge*edge);
  for(let i=0;i<valid.length;i++){
   if(!valid[i])continue;
   classes[i]=scl.get(i);
   for(let k=0;k<3;k++)data[4*i+k]=rgb.get(i,k);
   data[4*i+3]=255;
  }
  const soil=bareReflectance(data,classes,edge,edge);
  const bare=soil.measured?Buffer.from(encode({width:edge,height:edge,data:soil.output,channels:4})).toString('base64'):null;
  return {date,clear,bare,soilMeasured:soil.measured,soilEstimated:soil.estimated,png:Buffer.from(encode({width:edge,height:edge,data,channels:4})).toString('base64')};
 }
 return {};
}
