import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
const ttl=365*86400000, page=32768;
export function satelliteWindow(lat,lon,epoch){
 const west=lon<-128||lon>160||lat>52;
 const s=west?(epoch>=Date.UTC(2023,0,4,18)/1000?18:epoch>=Date.UTC(2019,1,13)/1000?17:0):(epoch>=Date.UTC(2025,3,7,15)/1000?19:epoch>=Date.UTC(2018,0,1)/1000?16:0);
 if(!s)return null;
 const product=west&&lat>30?'ACMF':'ACMC'; // Full disk includes Alaska; the West PACUS sector includes Hawaii.
 const t=new Date(epoch*1000),y=t.getUTCFullYear(),d=String(1+Math.floor((epoch-Date.UTC(y,0,1)/1000)/86400)).padStart(3,'0'),h=String(t.getUTCHours()).padStart(2,'0');
 const prefix='ABI-L2-'+product+'/'+y+'/'+d+'/'+h+'/OR_ABI-L2-'+product+'-';
 return {s,product,prefix,root:'https://noaa-goes'+s+'.s3.amazonaws.com/',time:y+d+h};
}
function epochOf(code){
 const y=+code.slice(0,4),d=+code.slice(4,7);
 return Date.UTC(y,0,d,+code.slice(7,9),+code.slice(9,11),+code.slice(11,13))/1000+Number(code.slice(13))/10;
}
export async function observedCloud(lat,lon,epoch,read){
 const w=satelliteWindow(lat,lon,epoch);if(!w)return {available:false,reason:'before_goes_r'};
 const info='metar-goes1-'+w.s+'-'+w.product+'-'+w.time;
 const xml=(await read(info+'.xml',w.root+'?list-type=2&max-keys=20&prefix='+w.prefix,ttl,null,5000,20000)).toString();
 let match;
 for(const m of xml.matchAll(/<Contents>.*?<Key>([^<]+)<\/Key>.*?<Size>(\d+)<\/Size>.*?<\/Contents>/gs)){
  const key=m[1],name=key.match(/_G(\d+)_s(\d{14})_e(\d{14})_c(\d{14})\.nc$/);
  if(!name||+name[1]!==w.s||!key.startsWith(w.prefix)||!/^[\w/.\-]+$/.test(key))continue;
  const mid=(epochOf(name[2])+epochOf(name[3]))/2,delta=mid-epoch,size=+m[2];
  if(size<=0||size>64000000||Math.abs(delta)>300)continue;
  if(!match||Math.abs(delta)<Math.abs(match.delta))match={url:w.root+key,size,delta,scan:name[4]};
 }
 if(!match)return {available:false,reason:'no_nearby_scan'};
 const cmd=process.argv[1].endsWith('.mjs')?[process.argv[1]]:[];
 const child=spawn(process.execPath,[...cmd,'archive-cloud-read',String(match.size),String(lat),String(lon)],{stdio:['pipe','pipe','ignore']});
 const lines=createInterface({input:child.stdout});
 const timer=setTimeout(()=>child.kill(),18000);
 let result,bytes=0,count=0;
 child.stdin.on('error',()=>{});
 try{
  for await(const line of lines){
   const request=JSON.parse(line);
   if(request.result){result=request.result;break;}
   const {position,length}=request;
   if(!Number.isSafeInteger(position)||position<0||position%page!==0||position>=match.size||length!==Math.min(page,match.size-position)||++count>24)throw Error('swath budget exceeded');
   const data=await read('metar-goes1-'+w.s+'-'+match.scan+'-'+position+'.bin',match.url,ttl,[position,position+length-1],6000,length);
   child.stdin.write(data);bytes+=data.length;
  }
 }finally{clearTimeout(timer);child.stdin.end();child.kill();}
 return result?{...result,delta_seconds:+match.delta.toFixed(1),url:match.url,bytes,ranges:count,platform:'GOES-'+w.s}:{available:false,reason:'unresolved_swath'};
}
