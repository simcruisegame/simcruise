const radius=6371.009;
const radians=d=>d*Math.PI/180;
function distance(a,b,c,d){
 const h=Math.sin(radians(c-a)/2)**2+Math.cos(radians(a))*Math.cos(radians(c))*Math.sin(radians(d-b)/2)**2;
 return radius*2*Math.asin(Math.sqrt(Math.min(1,h)));
}
const cover={CLR:0,SKC:0,NSC:0,FEW:.19,SCT:.44,BKN:.78,OVC:1,VV:1};
export async function conditions(lat,lon,read){
 const tileLat=Math.round(lat*2)/2,tileLon=Math.round(lon*2)/2;
 const areas=[[Math.max(-180,tileLon-.75),Math.min(180,tileLon+.75)]];
 if(tileLon-.75 < -180)areas.push([tileLon-.75+360,180]);
 if(tileLon+.75 > 180)areas.push([-180,tileLon+.75-360]);
 const rows=[];
 for(const [west,east] of areas){
  const bounds=[tileLat-.65,west,tileLat+.65,east].join(',');
  const url='https://aviationweather.gov/api/data/metar?format=json&hours=2&bbox='+bounds;
  const raw=await read('metar-'+bounds+'.json',url,15*60000);
  rows.push(...JSON.parse(raw.toString()));
 }
 const now=Date.now();
 const eligible=rows.filter(r=>Number.isFinite(r.lat)&&Number.isFinite(r.lon)&&Number.isFinite(r.obsTime)&&Math.abs(now-r.obsTime*1000)<2.5*3600000);
 for(const r of eligible)r.km=distance(lat,lon,r.lat,r.lon);
 const valid=eligible.filter(r=>r.km<=85&&r.cover in cover).sort((a,b)=>a.km-b.km||b.obsTime-a.obsTime);
 if(!valid.length)return {};
 const r=valid[0],visibility=parseFloat(r.visib);
 // CLR/SKC can coincide with an obscuration or precipitation at the airport.
 // NSC, absent visibility and distant stations do not establish clear blue sky.
 const clearSky=(r.cover==='CLR'||r.cover==='SKC')&&Number.isFinite(visibility)
  &&visibility*1609.344>=10000&&r.km<=40&&!String(r.wxString??'').trim();
 return {clear_sky:clearSky,station:r.icaoId,at:new Date(r.obsTime*1000).toISOString(),km:r.km,cloud:cover[r.cover],visibility_m:Number.isFinite(visibility)?visibility*1609.344:16093,description:({CLR:'clear',SKC:'clear',NSC:'mostly clear',FEW:'few clouds',SCT:'scattered clouds',BKN:'broken clouds',OVC:'overcast',VV:'obscured sky'})[r.cover]};
}
