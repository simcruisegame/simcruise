import {gunzipSync} from 'node:zlib';
const archiveTTL=45*86400000;

function unpack(bytes,compression) {
 if(compression===1)return bytes;
 if(compression===2)return gunzipSync(bytes,{maxOutputLength:10*1024*1024});
 throw Error('Unsupported PMTiles compression');
}
function directory(data) {
 let at=0;
 function value(){
  let n=0,p=1;
  while(at<data.length&&Number.isSafeInteger(p)){const b=data[at++];n+=(b&127)*p;if(b<128&&Number.isSafeInteger(n))return n;p*=128}
  throw Error('Invalid PMTiles integer');
 }
 const n=value();if(n>80000||n>data.length/4)throw Error('Directory outside budget');
 const rows=[];let id=0;
 for(let i=0;i<n;i++){id+=value();rows.push({id})}
 for(const row of rows)row.run=value();
 for(const row of rows)row.length=value();
 for(let i=0;i<n;i++){const raw=value();rows[i].offset=raw===0&&i?rows[i-1].offset+rows[i-1].length:raw-1}
 return rows;
}
function tileId(x,y,zoom) {
 let d=(4**zoom-1)/3;
 for(let s=2**(zoom-1);s>=1;s/=2){
  const a=Number(!!(x&s)),b=Number(!!(y&s));d+=s*s*((3*a)^b);
  if(!b){if(a){x=s-1-x;y=s-1-y}[x,y]=[y,x]}
 }
 return d;
}
export async function archiveTile(mx,my,release,read,theme='buildings') {
 const base=theme==='base';
 if(!base&&theme!=='buildings')throw Error('Unknown Overture archive');
 const zoom=base?13:14;
 const key=base?'ov-base-':'ov-';
 const url='https://overturemaps-extras-us-west-2.s3.us-west-2.amazonaws.com/tiles/'+release+'/'+theme+'.pmtiles';
 const range=(start,length)=>{
  if(!Number.isSafeInteger(start)||start<0||!Number.isSafeInteger(length)||length<=0||length>1200*1024)throw Error('Archive range outside budget');
  return read(key+release+'-'+start+'-'+length,url,archiveTTL,[start,start+length-1],3500);
 };
 const header=await range(0,16384);
 if(header.toString('ascii',0,7)!=='PMTiles'||header[7]!==3||header[99]!==1||header[100]>zoom||header[101]<zoom)throw Error('Incompatible PMTiles archive');
 const offset=i=>Number(header.readBigUInt64LE(i)),root=offset(8),len=offset(16);
 let bytes=root+len<=header.length?header.subarray(root,root+len):await range(root,len);
 const id=tileId(mx,my,zoom);
 for(let depth=0;depth<4;depth++) {
  const rows=directory(unpack(bytes,header[97]));let low=0,high=rows.length-1;
  while(low<=high){const m=(low+high)>>1;if(rows[m].id<=id)low=m+1;else high=m-1}
  const row=rows[high];if(!row)return null;
  if(row.run){if(id>=row.id+row.run)return null;return unpack(await range(offset(56)+row.offset,row.length),header[98])}
  bytes=await range(offset(40)+row.offset,row.length);
 }
 throw Error('Unexpected PMTiles directory depth');
}
