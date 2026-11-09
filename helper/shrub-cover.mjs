// Fractional shrub cover and height for the low, open rangeland around a driving district.
import fs from 'node:fs';
import path from 'node:path';
import {fromArrayBuffer} from 'geotiff';
import {decode, encode} from 'fast-png';
import palette from './shrub-palette.json' with {type:'json'};

const year=2025, size=256, world=40075016.68557849, day=86400000;
const verified=new Map(palette.map((rgb,percent)=>[parseInt(rgb,16),percent]));
function decodeFractions(bytes) {
 const im=decode(bytes);
 if(im.width!==size || im.height!==size || im.depth!==8 || im.channels!==4)throw Error('Unexpected RAP raster');
 const result=new Uint8Array(size*size);
 for(let i=0;i<result.length;i++) {
  const k=i*4;
  if(im.data[k+3]!==255)continue;
  const rgb=(im.data[k]<<16)|(im.data[k+1]<<8)|im.data[k+2];
  result[i]=verified.get(rgb)??0;
 }
 return result;
}
function area(x,y) {
 const step=world/4096,west=x*step-world*.5,north=world*.5-y*step;
 return {west,north,step,box:[west,north-step,west+step,north]};
}
async function registeredHeights(bytes, district) {
 const tif=await fromArrayBuffer(bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength));
 const im=await tif.getImage();
 if(im.getWidth()!==size||im.getHeight()!==size || im.getSamplesPerPixel()!==1 ||
    im.getGeoKeys()?.ProjectedCSTypeGeoKey!==3857)throw Error('Unexpected height raster');
 const [x0,bottom,right,y0]=im.getBoundingBox();
 const dx=(right-x0)/size, dy=(bottom-y0)/size;
 if(!(dx>0 && dy<0 && dx<100 && dy>-100))throw Error('Unexpected ground resolution');
 const heights=await im.readRasters({samples:[0],interleave:true,pool:null}),nodata=im.getGDALNoData();
 const out=new Uint8Array(size*size);
 for(let y=0;y<size;y++)for(let x=0;x<size;x++) {
  const j=Math.floor((district.north-(y+.5)*district.step/size-y0)/dy);
  const i=Math.floor((district.west+(x+.5)*district.step/size-x0)/dx);
  if(i<0 || j<0 || i>=size || j>=size)continue;
  const h=heights[j*size+i];
  if(h!==nodata && h>=8 && h<=250)out[y*size+x]=h;
 }
 return out;
}
function validMask(file) {
 const stat=fs.statSync(file,{throwIfNoEntry:false});
 if(!stat?.isFile() || stat.size<45 || stat.size>size*size*4 ||
    Date.now()-stat.mtimeMs>=180*day)return false;
 const bytes=fs.readFileSync(file);
 return bytes.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10])) &&
   bytes.readUInt32BE(16)===size && bytes.readUInt32BE(20)===size &&
   bytes[24]===8 && bytes[25]===2 && bytes.toString('ascii',bytes.length-8,bytes.length-4)==='IEND';
}

export async function shrubCover(x,y,cache,read) {
 if(![x,y].every(v=>Number.isInteger(v)&&v>=0&&v<4096))throw Error('Invalid shrub district');
 const base={id:[x,y],year,status:'unsupported'};
 const lat=Math.atan(Math.sinh(Math.PI*(1-(y+.5)/2048)))*180/Math.PI,lon=(x+.5)*360/4096-180;
 // Intersect the WCS advertised footprint with the continental driving region.
 if(lat<24 || lat>50 || lon< -125 || lon> -87.54)return base;
 const name='shrub-mask-v1-'+year+'-'+x+'-'+y+'.png';
 const target=path.join(cache,name);
 if(validMask(target))return {...base,status:'available',png:name};
 const rap=await read('shrub-rap-'+year+'-'+x+'-'+y,
  'https://storage.googleapis.com/usda-rap-tiles-cover-v3/masked/shr/'+year+'/12/'+x+'/'+y+'.png',365*day,null,4000,150000);
 const cover=decodeFractions(rap);
 const district=area(x,y);
 const output=new Uint8Array(size*size*3);
 // Ignore a district with only trace cover, then require a nonzero independent height per pixel.
 if(cover.some(val=>val>=4)) {
  const query=new URLSearchParams({service:'WCS',version:'1.0.0',request:'GetCoverage',coverage:'rcmap_shrub_height:rcmap_shrub_height_'+year,
   bbox:district.box.join(','),crs:'EPSG:3857',response_crs:'EPSG:3857',width:String(size),height:String(size),format:'GeoTIFF'});
  const raw=await read('shrub-rcmap-'+year+'-'+x+'-'+y,
   'https://www.mrlc.gov/geoserver/rcmap_shrub_height/wcs?'+query,365*day,null,5500,560000);
  const heights=await registeredHeights(raw,district);
  for(let i=0;i<cover.length;i++)if(cover[i]>=4 && heights[i]>0){
   output[i*3]=cover[i];output[i*3+1]=heights[i];
  }
 }
 const bytes=encode({width:size,height:size,channels:3,data:output}),temp=target+'.'+process.pid+'.tmp';
 try{fs.writeFileSync(temp,bytes);fs.renameSync(temp,target);}finally{fs.rmSync(temp,{force:true});}
 return {...base,status:'available',png:name};
}
