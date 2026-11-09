import fs from 'node:fs';
const page=32768;
function fromParent(position,length){
 fs.writeSync(1,JSON.stringify({position,length})+'\n');
 const bytes=Buffer.alloc(length);
 for(let i=0;i<length;){
  const n=fs.readSync(0,bytes,i,length-i,null);
  if(!n)throw Error('incomplete HDF page');
  i+=n;
 }
 return bytes;
}
// ABI fixed-grid projection, GOES-R PUG Vol 5 §4.2.8. All grid parameters are
// read from the actual requested file and each field's flags are verified.
export async function readCloud(size,lat,lon){
 if(!Number.isSafeInteger(size)||size<32768||size>64000000||!Number.isFinite(lat)||!Number.isFinite(lon))throw Error('invalid swath');
 const h5=(await import('./vendor/h5wasm/hdf5_hl.js')).default;
 const {FS}=await h5.ready;
 FS.writeFile('/scene.nc',new Uint8Array(0));
 const node=FS.lookupPath('/scene.nc').node;node.usedBytes=size;
 const pages=new Map();
 node.stream_ops={...node.stream_ops,read(stream,buffer,offset,length,pos){
  if(!Number.isSafeInteger(pos)||pos<0)throw Error('invalid swath offset');
  const count=Math.max(0,Math.min(length,size-pos));
  for(let done=0;done<count;){
   const start=Math.floor((pos+done)/page)*page;
   if(!pages.has(start)){
    if(pages.size>=24)throw Error('swath budget exceeded');
    pages.set(start,fromParent(start,Math.min(page,size-start)));
   }
   const at=pos+done-start,n=Math.min(count-done,page-at);
   buffer.set(pages.get(start).subarray(at,at+n),offset+done);done+=n;
  }
  return count;
 }};
 const file=new h5.File('/scene.nc','r');
 try{
  const proj=file.get('goes_imager_projection'),x=file.get('x'),y=file.get('y'),acm=file.get('ACM'),dqf=file.get('DQF');
  const attr=(ds,k)=>Number(ds.attrs[k].value);
  if(proj.attrs.sweep_angle_axis.value!=='x'||acm.attrs.flag_meanings.value!=='clear probably_clear probably_cloudy cloudy'||!dqf.attrs.flag_meanings.value.startsWith('good_quality_qf'))throw Error('unsupported NOAA flags');
  const a=attr(proj,'semi_major_axis'),b=attr(proj,'semi_minor_axis'),H=a+attr(proj,'perspective_point_height');
  const rad=Math.PI/180,phi=Math.atan((b/a)**2*Math.tan(lat*rad)),dl=(lon-attr(proj,'longitude_of_projection_origin'))*rad;
  const r=b/Math.sqrt(1-(1-(b/a)**2)*Math.cos(phi)**2),sx=H-r*Math.cos(phi)*Math.cos(dl),sy=-r*Math.cos(phi)*Math.sin(dl),sz=r*Math.sin(phi);
  const satelliteZenith=Math.acos(((H-sx)*sx-sy*sy-(a/b)**2*sz*sz)/(Math.hypot(H-sx,sy,(a/b)**2*sz)*Math.hypot(sx,sy,sz)))/rad;
  if(!Number.isFinite(satelliteZenith)||satelliteZenith>76)throw Error('excessive viewing angle');
  const px=(d,v)=>Math.round((v-attr(d,'add_offset'))/attr(d,'scale_factor'));
  const col=px(x,Math.asin(-sy/Math.hypot(sx,sy,sz))),row=px(y,Math.atan2(sz,sx));
  if(row<3||row+3>=acm.shape[0]||col<3||col+3>=acm.shape[1]||dqf.shape.join()!=acm.shape.join())throw Error('outside image');
  const region=[[row-3,row+4],[col-3,col+4]],classes=acm.slice(region),flags=dqf.slice(region),histogram=[0,0,0,0];
  if(classes?.length!==49||flags?.length!==49)throw Error('missing local pixels');
  for(let i=0;i<49;i++)if(flags[i]===0&&classes[i]<4)histogram[classes[i]]++;
  const clear=histogram[0]+histogram[1],cloudy=histogram[2]+histogram[3];
  return {available:true,good:clear+cloudy,clear,cloudy,histogram,pixel:[row,col],viewZenith:+satelliteZenith.toFixed(1)};
 }finally{file.close();}
}
