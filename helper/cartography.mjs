import {classifyRings} from '@mapbox/vector-tile';
import {vectorTile} from './vector-tile.mjs';
import {gunzipSync} from 'node:zlib';
export async function cartography(z,x,y,read){
 const meta=JSON.parse((await read('manifest','https://tiles.openfreemap.org/planet',86400000)).toString());
 const url=meta.tiles[0].replace('{z}',z).replace('{x}',x).replace('{y}',y);
 const buf=await read(z===14?'v-'+x+'-'+y:'vmap-'+z+'-'+x+'-'+y,url);
 const tile=vectorTile(buf[0]===31?gunzipSync(buf):buf);
 const out={lines:[],fills:[],names:[]};
 for(const layerName of ['water','landcover','boundary','transportation','transportation_name','place']){
  const layer=tile.layers[layerName];if(!layer)continue;
  for(let i=0;i<layer.length;i++){
   const f=layer.feature(i),p=f.properties,cls=p.class??'',n=p['name:en']||p.name||p.ref;
   if(layerName==='landcover'&&!['wood','forest','grass'].includes(cls))continue;
   const paths=f.loadGeometry();const loc=pt=>[Math.round(pt.x/f.extent*4096),Math.round(pt.y/f.extent*4096)];
   if(layerName==='place'){
    if(n&&paths[0]?.length)out.names.push([n,...loc(paths[0][0]),+(p.rank??8),1]);
   }else if(layerName==='transportation_name'){
    if(!n||!paths[0]?.length)continue;
    const pts=paths.reduce((a,b)=>a.length>b.length?a:b);out.names.push([n,...loc(pts[pts.length>>1]),['motorway','trunk'].includes(cls)?4:7,0]);
   }else if(f.type===3){
    // Preserve holes by feeding the Godot canvas triangulated polygon indices.
    for(const poly of classifyRings(paths)){
     let rings=poly.map(r=>r.slice(0,-1).map(loc)).filter(r=>r.length>=3);
     if(rings.length)out.fills.push([layerName==='water'?0:1,rings]);
    }
   }else if(f.type===2){
    const kind=layerName==='boundary'?0:({motorway:5,trunk:5,primary:4,secondary:3,tertiary:2,minor:1,service:1})[cls];
    if(kind===undefined||layerName==='boundary'&&Number(p.admin_level)>6)continue;
    for(const path of paths)if(path.length>=2)out.lines.push([kind,path.map(loc)]);
   }
  }
 }
 out.names.sort((a,b)=>a[3]-b[3]);return out;
}
