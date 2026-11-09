// NASA Terra daily science-product visualization. Only opaque palette entries
// documented as an NDSI measurement may change a rendered ground pixel.
import {decode,encode} from 'fast-png';
import values from './snow-colors.json' with {type:'json'};

const earth=40075016.68557849, day=86400000, pixels=256;
const rgbIndex=new Map(Object.entries(values).map(([key,val])=>[parseInt(key,16),val]));
function readTile(raw) {
 const src=decode(raw);
 if(src.width!==256||src.height!==256||src.depth!==8||src.channels!==1||!Array.isArray(src.palette)) throw Error('Unexpected NASA snow tile');
 const lookup=src.palette.map(([r,g,b,a])=>a===255?(rgbIndex.get((r<<16)|(g<<8)|b)||0):0);
 return Uint8Array.from(src.data,k=>lookup[k]||0);
}
export async function regionalSnow(x,y,date,read) {
 if(![x,y].every(v=>Number.isInteger(v)&&v>=0&&v<8192))throw Error('Invalid relief region');
 const acquired=new Date(date+'T00:00:00Z');
 if(!/^\d{4}-\d{2}-\d{2}$/.test(date)||!Number.isFinite(+acquired)||acquired.toISOString().slice(0,10)!==date||date<'2000-02-24'||+acquired>Date.now())throw Error('Invalid observation date');
 const u=(x+.5)/8192,v=(y+.5)/8192,lat=Math.atan(Math.sinh(Math.PI*(1-2*v)));
 const half=45000/(earth*Math.cos(lat)),west=u-half,north=v-half,step=2*half/pixels;
 const cells=[];
 for(let ty=Math.floor(north*256);ty<=Math.floor((v+half)*256);ty++) {
  for(let tx=Math.floor(west*256);tx<=Math.floor((u+half)*256);tx++) {
   if(ty>=0 && ty<256)cells.push({tx,ty,d:Math.hypot(tx+.5-u*256,ty+.5-v*256)});
  }
 }
 // At Alaska's higher latitudes the visible square can cross more tiles. Start
 // at the observer and retain missing data in any outer cells beyond the cap.
 const raster=new Uint8Array(pixels*pixels),tiles=new Map();
 const ordered=cells.sort((a,b)=>a.d-b.d).slice(0,4);
 let complete=0;
 for(const {tx,ty} of ordered) {
  const nx=(tx%256+256)%256,key=tx+','+ty;
  const name='snow-terra-'+date+'-8-'+nx+'-'+ty;
  const url='https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/MODIS_Terra_L3_NDSI_Snow_Cover_Daily/default/'+date+'/GoogleMapsCompatible_Level8/8/'+ty+'/'+nx+'.png';
  try {
   const data=await read(name,url,7*day,null,3300,125000);
   tiles.set(key,readTile(data));complete++;
  } catch { /* Preserve ordinary ground in any unobserved tile. */ }
 }
 let positive=0;
 for(let row=0;row<pixels;row++){
  const iy=Math.floor((north+(row+.5)*step)*65536),ty=Math.floor(iy/256);
  for(let col=0;col<pixels;col++){
   const ix=Math.floor((west+(col+.5)*step)*65536),tx=Math.floor(ix/256);
   const tile=tiles.get(tx+','+ty);
   if(tile) {
    const ndsi=tile[(iy-ty*256)*256+ix-tx*256];
    if(ndsi>=40){raster[row*pixels+col]=Math.round(ndsi*2.55);positive++;}
   }
  }
 }
 return {region:[x,y],date,tiles:complete,expected:ordered.length,positive,status:complete?'available':'unavailable',
  mask:positive?Buffer.from(encode({width:pixels,height:pixels,channels:1,data:raster})).toString('base64'):null};
}
