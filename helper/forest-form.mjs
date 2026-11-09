// Small zoom-14 stand-form observations. Georeference each categorical response independently.
import fs from 'node:fs';
import path from 'node:path';
import {fromArrayBuffer} from 'geotiff';
import legend from './forest-types-2025.json' with {type:'json'};

const day=86400000;
const extent=40075016.68557849;
const size=16384;
const supported=new Set(Object.keys(legend.classes).map(Number));

function product(lat,lon) {
 if(lat>=24&&lat<=51&&lon>=-125&&lon<=-66) return ['CONUS','NLCD_2021_Land_Cover_L48',2021];
 if(lat>=50.5&&lat<=72.5&&(lon<=-129.5||lon>=169)) return ['AK','NLCD_2016_Land_Cover_AK',2016];
 return null;
}

async function raster(bytes,expected,bits) {
 if(bytes.byteLength<256||bytes.byteLength>98304)throw Error('Invalid forest raster size');
 const tif=await fromArrayBuffer(bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength));
 const image=await tif.getImage();
 const dir=image.fileDirectory;
 const sr=image.getGeoKeys();
 if(image.getWidth()!==expected||image.getHeight()!==expected||image.getSamplesPerPixel()!==1||
    image.getBitsPerSample()!==bits||dir.getValue('Compression')!==1||sr.ProjectedCSTypeGeoKey!==3857||sr.GTRasterTypeGeoKey!==1)throw Error('Unexpected forest raster grid');
 const [west,north]=image.getOrigin();
 const matrix=dir.getValue('ModelTransformation');
 if(matrix&&(matrix[1]!==0||matrix[4]!==0))throw Error('Rotated forest raster');
 // GeoServer returns the north-up signed ModelTransformation. ArcGIS uses unsigned pixel scales.
 const [dx,dy]=matrix?[matrix[0],matrix[5]]:image.getResolution();
 if(!(dx>0&&dy<0&&Number.isFinite(west)&&Number.isFinite(north)))throw Error('Invalid forest geotransform');
 const values=await image.readRasters({interleave:true,samples:[0]});
 return (x,y)=>{
  const i=Math.floor((x-west)/dx),j=Math.floor((y-north)/dy);
  return i<0||j<0||i>=expected||j>=expected?0:values[j*expected+i];
 };
}

export async function forestForm(x,y,cache,read,now=Date.now()) {
 if(!Number.isInteger(x)||!Number.isInteger(y)||x<0||y<0||x>=size||y>=size)throw Error('Invalid forest tile');
 const lat=Math.atan(Math.sinh(Math.PI*(1-2*(y+.5)/size)))*180/Math.PI;
 const lon=(x+.5)/size*360-180;
 const zone=product(lat,lon);
 const result={version:1,tile:[x,y],zoom:14,evtYear:2025,nlcdYear:zone?.[2]??null,legendSha256:legend.sha256};
 if(!zone)return {...result,status:'unsupported'};
 const prefix='forest-v1-'+x+'-'+y;
 const output=path.join(cache,prefix+'.json');
 try {
  const age=Math.max(0,now-fs.statSync(output).mtimeMs);
  const saved=JSON.parse(fs.readFileSync(output));
  if(age>=0&&age<((saved.status==='unavailable'||saved.stale)?30*60000:180*day)&&saved.version===1&&saved.tile?.[0]===x&&saved.tile?.[1]===y&&saved.evtYear===2025&&saved.nlcdYear===zone[2]&&saved.legendSha256===legend.sha256) return saved;
 }catch{}
 const side=extent/size,west=(x/size-.5)*extent,north=(.5-y/size)*extent;
 const grid=Math.max(12,Math.round(side*Math.cos(lat*Math.PI/180)/30)),step=side/grid,edge=grid+2;
 const bbox=[west-step,north-side-step,west+side+step,north+step].map(n=>n.toFixed(6)).join(',');
 const evtService='https://lfps.usgs.gov/arcgis/rest/services/Landfire_LF2025/LF2025_EVT_'+zone[0]+'/ImageServer/exportImage';
 const nlcdService='https://www.mrlc.gov/geoserver/mrlc_download/'+zone[1]+'/wcs';
 const params1=new URLSearchParams({bbox,bboxSR:'3857',imageSR:'3857',size:edge+','+edge,pixelType:'S16',format:'tiff',interpolation:'RSP_NearestNeighbor',renderingRule:'{"rasterFunction":"None"}',f:'image'});
 const params2=new URLSearchParams({service:'WCS',version:'1.0.0',request:'GetCoverage',coverage:'mrlc_download:'+zone[1],crs:'EPSG:3857',response_crs:'EPSG:3857',bbox,width:String(edge),height:String(edge),format:'GeoTIFF',interpolation:'nearest neighbor'});
 async function sample(layer,url,bits) {
  const key=prefix+'-'+layer+'.tif';
  const bytes=await read(key,url,365*day,null,5500,98304);
  try{return await raster(bytes,edge,bits)}
  catch(error){fs.rmSync(path.join(cache,key),{force:true});throw error}
 }
 let status;
 try {
  const evt=await sample('evt',evtService+'?'+params1,16);
  const candidates=new Uint8Array(edge*edge);
  for(let j=0;j<edge;j++)for(let i=0;i<edge;i++){
   candidates[j*edge+i]=Number(supported.has(evt(west+(i-.5)*step,north-(j-.5)*step)));
  }
  let count=0;
  const bits=Buffer.alloc(Math.ceil(grid*grid/8));
  // Avoid the second provider entirely in urban, desert or unsupported forest types.
  if(candidates.some(Boolean)){
   const cover=await sample('cover',nlcdService+'?'+params2,8);
   for(let j=0;j<edge;j++)for(let i=0;i<edge;i++){
    if(candidates[j*edge+i]&&cover(west+(i-.5)*step,north-(j-.5)*step)!==42)candidates[j*edge+i]=0;
   }
   for(let j=0;j<grid;j++)for(let i=0;i<grid;i++){
    const k=(j+1)*edge+i+1;
    if(!candidates[k]||candidates[k-1]+candidates[k+1]+candidates[k-edge]+candidates[k+edge]<2)continue;
    const bit=j*grid+i;bits[bit>>3]|=1<<(bit&7);count++;
   }
  }
  status={...result,status:count?'available':'no_coverage',grid,accepted:count,...(count?{bits:bits.toString('base64')}:{})};
 }catch(error){status={...result,status:'unavailable',reason:String(error.message).slice(0,120)}}
 // A provider failure cannot replace an already published positive observation.
 let prior;
 try{prior=JSON.parse(fs.readFileSync(output))}catch{}
 if(status.status==='unavailable'&&prior?.status==='available'&&prior.legendSha256===legend.sha256&&prior.tile?.[0]===x&&prior.tile?.[1]===y)status={...prior,stale:true};
 const temp=output+'.'+process.pid+'.'+crypto.randomUUID()+'.tmp';
 try{fs.writeFileSync(temp,JSON.stringify(status));fs.renameSync(temp,output)}finally{fs.rmSync(temp,{force:true})}
 return status;
}
