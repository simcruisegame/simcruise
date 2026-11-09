import {inflateSync} from 'node:zlib';

// CHMv2 uses Web Mercator zoom-10 COGs with one 512-pixel block per zoom-16 cell.
function quadkey(x,y) {
 let key='';
 for(let bit=9;bit>=0;bit--)key+=((x>>bit)&1)+2*((y>>bit)&1);
 return key;
}
function block(header,index) {
 if(header.toString('ascii',0,2)!=='II'||header.readUInt16LE(2)!==42)throw Error('Unknown canopy format');
 const ifd=header.readUInt32LE(4),count=header.readUInt16LE(ifd),tags=new Map();
 for(let i=0;i<count;i++) {
  const p=ifd+2+12*i;
  tags.set(header.readUInt16LE(p),[header.readUInt16LE(p+2),header.readUInt32LE(p+4),header.readUInt32LE(p+8)]);
 }
 const val=(tag,i=0)=>{const [type,n,data]=tags.get(tag);if(n===1)return data;return type===3?header.readUInt16LE(data+2*i):header.readUInt32LE(data+4*i)};
 if(val(256)!==32768||val(257)!==32768||val(322)!==512||val(323)!==512||val(258)!==8||val(259)!==8||val(317)!==1)throw Error('Unsupported canopy grid');
 return [val(324,index),val(325,index)];
}
function crowns(image,side) {
 const grid=128,heights=new Uint8Array(grid*grid),peaks=[];
 for(let y=0;y<grid;y++)for(let x=0;x<grid;x++) {
  let sum=0;for(let j=0;j<4;j++)for(let i=0;i<4;i++)sum+=image[(4*y+j)*512+4*x+i];
  heights[y*grid+x]=Math.round(sum/16);
 }
 for(let y=1;y<127;y++)for(let x=1;x<127;x++) {
  const i=y*grid+x,h=heights[i];
  if(h<3||h<heights[i-1]||h<heights[i+1]||h<heights[i-128]||h<heights[i+128])continue;
  peaks.push([i,h]);
 }
 peaks.sort((a,b)=>b[1]-a[1]||((a[0]*7919)%16381)-((b[0]*7919)%16381));
 const used=new Uint8Array(grid*grid),result=[];
 for(const [index,h] of peaks) {
  if(used[index])continue;
  const x=index%128,y=index>>7;
  let span=0;
  for(const [dx,dy] of [[1,0],[-1,0],[0,1],[0,-1]]) {
   let k=1;while(k<5&&x+dx*k>=0&&x+dx*k<128&&y+dy*k>=0&&y+dy*k<128&&heights[(y+dy*k)*128+x+dx*k]>h*.65)k++;
   span+=k;
  }
  const width=Math.min(h*.98,Math.max(h*.30,span*.5*side/128));
  const gap=Math.max(1.4,width*.69*128/side),radius=Math.ceil(gap);
  for(let j=Math.max(0,y-radius);j<=Math.min(127,y+radius);j++)for(let k=Math.max(0,x-radius);k<=Math.min(127,x+radius);k++)if((j-y)**2+(k-x)**2<=gap*gap)used[j*128+k]=1;
  result.push([(x+.5)/128,(y+.5)/128,h,Math.round(width*10)/10]);
 }
 return result;
}
export async function canopyObservation(x,y,read) {
 const key=quadkey(x>>6,y>>6),url='https://dataforgood-fb-data.s3.amazonaws.com/forests/v2/global/dinov3_global_chm_v2_ml3/chm/'+key+'.tif';
 const header=await read('c2-header-'+key,url,365*86400000,[0,65535]);
 const [offset,size]=block(header,(y%64)*64+x%64);
 if(!size)return {crowns:[],grid:null};
 const compressed=await read('c2-'+x+'-'+y,url,365*86400000,[offset,offset+size-1]);
 const image=inflateSync(compressed,{maxOutputLength:512*512});
 if(image.length!==512*512)throw Error('Incomplete canopy cell');
 return {crowns:crowns(image,611.496/Math.cosh(Math.PI*(1-2*(y+.5)/65536))),grid:image};
}

export async function canopy(x,y,read) {
 return (await canopyObservation(x,y,read)).crowns;
}
