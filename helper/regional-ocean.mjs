// Natural Earth ocean geometry for US regions. Land below sea level retains
// its original DEM, including inland basins such as Death Valley.
import {inflateSync} from 'node:zlib';
import catalog from './coast-atlas.json' with {type:'json'};

export function oceanSampler() {
 const decoded = new Map();
 return (longitude,latitude) => {
  const lon = longitude>=180?longitude-360:longitude < -180?longitude+360:longitude;
  const col = Math.floor(lon), row = Math.floor(latitude);
  const key = col+','+row;
  const entry = catalog.tiles[key];
  if(!entry || entry===1)return entry===1;
  let bits=decoded.get(key);
  if(!bits){
   bits=inflateSync(Buffer.from(entry,'base64'));
   decoded.set(key,bits);
  }
  const x=Math.min(255,Math.floor((lon-col)*256));
  const y=Math.min(255,Math.floor((row+1-latitude)*256));
  const index=y*256+x;
  return (bits[index>>3] & (128>>(index&7)))!==0;
 };
}
