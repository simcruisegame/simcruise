import index from './photo-weather-stations.json' with {type:'json'};

// archive_begin/end and site coordinates from the Iowa Environmental Mesonet AZOS catalog.
// This summarizes two observed moments above an airport. CLR means clear below sensor range.
const coverage = {CLR:0,SKC:0,FEW:0.1875,SCT:0.4375,BKN:0.75,OVC:1};
const columns = ['skyc1','skyc2','skyc3','skyc4','skyl1','skyl2','skyl3','skyl4','vsby','wxcodes','metar'];
const day = 86400;
function distance(lat,lon,otherLat,otherLon) {
 const rad = Math.PI/180;
 const a = Math.sin((lat-otherLat)*rad/2)**2 + Math.cos(lat*rad)*Math.cos(otherLat*rad)*Math.sin((lon-otherLon)*rad/2)**2;
 return 12742*Math.asin(Math.min(1,Math.sqrt(a)));
}
function utc(epoch){return new Date(epoch*1000).toISOString();}
export function archiveSlice(lat,lon,epoch,elevation,now=Date.now()/1000) {
 if(![lat,lon,epoch].every(Number.isFinite)||Math.abs(lat)>90||Math.abs(lon)>180||epoch<631152000||epoch>now-2*day)return null;
 const date=utc(epoch).slice(0,10);
 const sites=index.filter(([id,y,x,z,begin,end])=>Math.abs(y-lat)<.28&&(!begin||begin<=date)&&(!end||end>=date)&&
  (!Number.isFinite(elevation)||(z!==null&&Math.abs(z-elevation)<=200)))
  .map(([id,y,x])=>({id,y,x,km:distance(lat,lon,y,x)})).filter(site=>site.km<=30).sort((a,b)=>a.km-b.km);
 const near=sites.filter((s,i)=>sites.findIndex(v=>v.id===s.id)===i).slice(0,2);
 if(!near.length)return {near};
 const block=Math.floor(epoch/10800)*10800;
 const key='metar-archive1-'+near.map(s=>s.id).sort().join('-')+'-'+block+'.csv';
 const url=new URL('https://mesonet.agron.iastate.edu/cgi-bin/request/asos.py');
 for(const {id} of near)url.searchParams.append('station',id);
 for(const field of columns)url.searchParams.append('data',field);
 const stamp=t=>utc(t).slice(0,16)+'Z';
 for(const [k,v] of Object.entries({sts:stamp(block-3600),ets:stamp(block+14400),tz:'Etc/UTC',format:'onlycomma',latlon:'yes',direct:'no'}))url.searchParams.set(k,v);
 for(const type of ['3','4'])url.searchParams.append('report_type',type);
 return {near,key,url:url.href};
}
function bracket(lines,site,epoch) {
 const observations=[];
 for(const line of lines){
  const [id,when,longitude,latitude,...rest]=line.split(',');
  if(!Number.isFinite(Number(latitude))||!Number.isFinite(Number(longitude))||id!==site.id||distance(site.y,site.x,Number(latitude),Number(longitude))>1)continue;
  const t=Date.parse(when.replace(' ','T')+':00Z')/1000;
  const levels=rest.slice(0,4), layers=levels.filter(code=>Object.hasOwn(coverage,code));
  const cloud=layers.length?Math.max(...layers.map(code=>coverage[code])):NaN;
  const mi=Number(rest[8]), wx=rest[9]||'',raw=rest.slice(10).join(',').split(' RMK')[0];
  // A null or unrecognized adjacent report is a break in usable coverage.
  const unobscured=Number.isFinite(cloud)&&!levels.includes('VV')&&!/\b(NIL|TS|FZFG)\b/.test(raw)&&
    !/(RA|SN|TS|FG|FZ|DZ|FU|DU|BL|SQ|FC|SS|VA|PL|UP|GR|GS|IC|PO|SA|PY|DS)/.test(wx)&&(!Number.isFinite(mi)||mi>=3);
  const row={t,cloud,mi,valid:unobscured,codes:layers.join('/'),wx};
  if(Number.isFinite(t))observations.push(row);
 }
 observations.sort((a,b)=>a.t-b.t);
 const before=observations.filter(v=>v.t<=epoch).at(-1), after=observations.find(v=>v.t>epoch);
 if(!before||!after||!before.valid||!after.valid||epoch-before.t>65*60||after.t-epoch>65*60||after.t-before.t>95*60)return null;
 const f=(epoch-before.t)/(after.t-before.t);
 const obs={station:site.id,stationKm:+site.km.toFixed(2),utc:[utc(before.t),utc(after.t)],layers:[before.codes,after.codes],
  cloud:before.cloud+(after.cloud-before.cloud)*f,change:Math.abs(after.cloud-before.cloud),visMiles:[before.mi,after.mi]};
 if([before.mi,after.mi].every(Number.isFinite)){
  obs.visibilityAtLeast10SM=before.mi>=10&&after.mi>=10;
  obs.visibility_m=Math.round(1609.344*(before.mi+(after.mi-before.mi)*f));
 }
 return obs;
}
export async function photoWeather(lat,lon,epoch,elevation,read) {
 const slice=archiveSlice(lat,lon,epoch,elevation);
 if(!slice)return {status:'insufficient',reason:'invalid_or_recent_capture'};
 if(!slice.near.length)return {status:'insufficient',reason:'no_nearby_archive'};
 let raw;
 try{raw=(await read(slice.key,slice.url,365*day*1000,null,10000,98304)).toString();}
 catch{return {status:'unavailable',reason:'archive_temporarily_unavailable'};}
 const [header,...lines]=raw.trim().split(/\r?\n/);
 if(header!=='station,valid,lon,lat,'+columns.join(','))return {status:'unavailable',reason:'archive_format',key:slice.key};
 const reports=slice.near.map(site=>bracket(lines,site,epoch)).filter(Boolean);
 const src={provider:'IEM ASOS/METAR',key:slice.key,source:slice.url,reports,bytes:Buffer.byteLength(raw)};
 if(!reports.length)return {...src,status:'insufficient',reason:'no_bracketing_sky'};
 const [s,other]=reports;
 if(s.change>.3 || other&&(other.change>.3||Math.abs(s.cloud-other.cloud)>.30))return {...src,status:'insufficient',reason:'variable_or_conflicting_sky'};
 if(s.cloud>.19&&s.cloud<.70)return {...src,status:'insufficient',reason:'patchy_sky'};
 const clear=s.cloud<=.19;
 const visRange=s.visMiles.every(Number.isFinite)?Math.abs(s.visMiles[0]-s.visMiles[1]):0;
 const confidence=Math.max(.25,Math.min(.94,.94-.009*s.stationKm-.6*s.change-.025*visRange-(other?0:.06)));
 return {...src,status:'available',station:s.station,km:s.stationKm,kind:clear?'mostly_clear':'broken_or_overcast',
  cloud:+s.cloud.toFixed(3),confidence:+confidence.toFixed(3),visibility_m:s.visibility_m??null,visibilityAtLeast10SM:s.visibilityAtLeast10SM??false};
}
