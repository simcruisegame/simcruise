import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {test} from 'node:test';
import {deflateSync} from 'node:zlib';
import {encode,decode} from 'fast-png';
import {groundMask,selectRaster,spectralGround} from '../helper/spectral-ground.mjs';

const now=Date.UTC(2026,10,7),x=19880,y=23726,half=20037508.342789244,side=2*half/65536;
const west=x*side-half,north=half-y*side,bounds=[west,north-side,west+side,north];
const ring=[[west-5,north+5],[west+side+5,north+5],[west+side+5,north-side-5],[west-5,north-side-5],[west-5,north+5]];
const item={attributes:{objectid:3,name:'m_4407034_se_19_030_20230927_20240103',category:1,year:2023,lowps:.3},geometry:{rings:[ring]}};
const catalog={features:[item]};
function cir(){
 const data=new Uint8Array(256*256*4);
 for(let i=0;i<256*256;i++)data.set(i%256<128?[90,96,101,255]:[161,93,96,255],i*4);
 return encode({width:256,height:256,channels:4,data});
}
test('eligibility uses a complete, dated growing-season NAIP raster',()=>{
 assert.equal(selectRaster(catalog,bounds,now).id,3);
 for(const [name,year] of [['HRO_rgb',2023],['m_4407034_se_19_030_20230127',2023],['m_4407034_se_19_030_20180721',2018],['m_4407034_se_19_030_20230931',2023]]){
  const f=structuredClone(item);Object.assign(f.attributes,{name,year});
  assert.equal(selectRaster({features:[f]},bounds,now),null);
 }
 assert.equal(selectRaster(catalog,[west-side,north-side,west+side,north],now),null);
 assert.equal(selectRaster({...catalog,exceededTransferLimit:true},bounds,now),null);
});
test('near infrared needs confirmed low canopy and leaves hard surfaces alone',()=>{
 const canopy=Buffer.alloc(512*512);canopy[2*128]=15;
 const decoded=decode(groundMask(Buffer.from(cir()),canopy));
 assert.equal(decoded.data[14],0);assert.equal(decoded.data[128],0);assert.equal(decoded.data[180],255);
 const copied=new Uint8Array(256*256*4).fill(120);for(let j=3;j<copied.length;j+=4)copied[j]=255;
 assert.equal(groundMask(Buffer.from(encode({width:256,height:256,channels:4,data:copied})),canopy),null);
});
test('one locked catalog item, warm cache, unsupported bands',async t=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'sd-ground-cover-'));t.after(()=>fs.rmSync(dir,{recursive:true,force:true}));
 fs.writeFileSync(path.join(dir,'c2-'+x+'-'+y),deflateSync(Buffer.alloc(512*512)));
 const times=[];
 const fetcher=async url=>{
  times.push(Date.now());const u=new URL(url);
  if(u.pathname.endsWith('/query'))return Response.json(catalog);
  if(u.pathname.endsWith('/info'))return Response.json({bandCount:4,pixelType:'U8'});
  assert.equal(u.searchParams.get('bandIds'),'3,0,1');
  assert.deepEqual(JSON.parse(u.searchParams.get('mosaicRule')).lockRasterIds,[3]);
  return new Response(cir());
 };
 const result=await spectralGround(x,y,dir,fetcher,now);
 assert.equal(result.status,'available');assert.equal(result.requests,3);assert(result.bytes<=200*1024);
 assert(times[1]-times[0]>=990&&times[2]-times[1]>=990);
 const warm=await spectralGround(x,y,dir,()=>assert.fail('unexpected network'),now);
 assert.equal(warm.requests,0);assert.equal(warm.bytes,0);assert.equal(warm.status,'available');
 for(const name of fs.readdirSync(dir))if(name.startsWith('naip-low'))fs.rmSync(path.join(dir,name));
 let count=0;
 const rejected=await spectralGround(x,y,dir,async u=>{count++;return Response.json(u.includes('/query?')?catalog:{bandCount:3,pixelType:'U8'})},now);
 assert.equal(rejected.status,'unsupported');assert.equal(count,2);
});
