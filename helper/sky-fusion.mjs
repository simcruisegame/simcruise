// Class labels summarize the local satellite field, while station visibility and
// low clouds come from the bracketing surface observer. Neither sees a cloud's azimuth.
export function fuseSky(report,swath){
 if(!report.reports?.length)return report;
 if(!swath?.available)return {...report,status:!swath||swath.reason==='unresolved_swath'?'unavailable':'insufficient',reason:swath?.reason??'satellite_temporarily_unavailable'};
 const source=report.reports[0];
 if(swath.good<40||Math.abs(swath.delta_seconds)>300)return {...report,status:'insufficient',reason:'weak_satellite_support',satellite:swath};
 const clear=swath.clear/swath.good, cloudy=swath.cloudy/swath.good;
 const context={...report,station:source.station,km:source.stationKm,visibility_m:source.visibility_m,visibilityAtLeast10SM:source.visibilityAtLeast10SM,satellite:swath};
 const stationCloud=source.cloud;
 // Airport layers include cloud outside the satellite's overhead neighborhood.
 // A FEW or SCT report cannot justify clearing that entire viewing hemisphere.
 const skyClearAcrossReports=report.reports.every(s=>s.layers.every(v=>/^(CLR|SKC)$/.test(v)));
 if(clear>=.90&&!skyClearAcrossReports)return {...context,status:'insufficient',reason:'reported_cloud_outside_clear_satellite_patch'};
 if(clear>=.90&&skyClearAcrossReports){
  const confidence=.89-.005*source.stationKm-.12*source.change;
  const haze=Number.isFinite(source.visibility_m)?Math.min(.24,Math.max(0,(16093-source.visibility_m)/16093*.55)):0;
  return {...context,status:'available',reason:'locally_clear',kind:'scattering',cloud:.06,confidence,veil:haze};
 }
 if(cloudy>=.90){
  // An automated CLR report commonly omits cloud above its ceiling. The
  // satellite observes overhead cloud, but its depth and the sun occlusion are unknown.
  return {...context,status:'available',reason:'locally_cloudy',kind:'broken_or_overcast',cloud:stationCloud>=.7?.88:.64,confidence:stationCloud>=.7?.86:.68};
 }
 return {...context,status:'insufficient',reason:'patchy_satellite'};
}
