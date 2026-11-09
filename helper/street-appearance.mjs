// Estimate scene color from repeated, upright daylight views of the same road.
// The linear values are decoded photographic colors. Exposure and white balance are unknown.
import { execFile } from 'node:child_process';
import { mkdtemp, readFile, rm, stat } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { promisify } from 'node:util';

const run = promisify(execFile);
const degrees = Math.PI / 180;
const clamp = (n, low, high) => Math.max(low, Math.min(high, n));
const round = n => Math.round(n * 10000) / 10000;
const lumen = ([r, g, b]) => .2126*r + .7152*g + .0722*b;
const span = v => Math.max(...v) - Math.min(...v);
const difference = (a, b) => Math.max(...a.map((v, i) => Math.abs(v - b[i])));

function percentile(values, q = .5) {
  const sorted = [...values].sort((a,b) => a-b);
  return sorted[Math.floor(q * (sorted.length - 1))];
}
const center = colors => [0,1,2].map(c => percentile(colors.map(rgb => rgb[c])));

function srgbToLinear(v) {
  return v <= .04045 ? v / 12.92 : ((v + .055) / 1.055) ** 2.4;
}

function routeGeometry(frames) {
  const unique = new Set(frames.map(frame => String(frame.id)));
  if (frames.length < 3 || unique.size < 3) return {reason:'insufficient_distinct_photos'};
  if (unique.size !== frames.length || frames.some(f => !f.id || !f.sequenceId || f.sequenceId !== frames[0].sequenceId))
    return {reason:'mixed_or_duplicate_sequence'};
  if (frames.some(f => !Number.isInteger(f.sequenceIndex) || !Number.isFinite(f.lat) || !Number.isFinite(f.lon) ||
      Math.abs(f.lat) > 85 || Math.abs(f.lon) > 180 || !Number.isFinite(f.heading) || f.heading < 0 || f.heading >= 360))
    return {reason:'missing_pose'};
  if (frames.some(f => !/^(plane|planar|perspective|rectilinear|pinhole)$/i.test(f.cameraType ?? '')))
    return {reason:'unsupported_camera'};
  const ordered = [...frames].sort((a,b) => a.sequenceIndex-b.sequenceIndex);
  const angle = (a,b) => Math.abs(((a-b+540)%360)-180);
  let distance = 0, previousCourse;
  for (let i=1; i<ordered.length; i++) {
    const a = ordered[i-1], b = ordered[i];
    const di = b.sequenceIndex - a.sequenceIndex;
    const east = ((b.lon-a.lon+540)%360-180)*111195*Math.cos((b.lat+a.lat)*degrees*.5);
    const north = (b.lat-a.lat)*111195;
    const step = Math.hypot(east,north);
    const course = (Math.atan2(east,north)/degrees+360)%360;
    if (di < 1 || di > 40 || step < 3 || step > 145) return {reason:'unmatched_route_spacing'};
    if (angle(a.heading,b.heading)>30 || (previousCourse!==undefined && angle(course,previousCourse)>36))
      return {reason:'turning_or_unstable_heading'};
    // Travel may be forward or rearward along an axis. Side-looking cameras are unsuitable.
    const discrepancy = Math.max(...[a.heading,b.heading].map(h => Math.min(angle(h,course),angle(h,(course+180)%360))));
    if (discrepancy>34) return {reason:'view_route_disagreement'};
    const times=[a.capturedAt,b.capturedAt].map(t => t ? Date.parse(t) : NaN);
    if (times.every(Number.isFinite) && Math.abs(times[1]-times[0])>180000) return {reason:'noncontiguous_capture_time'};
    distance += step;
    previousCourse = course;
  }
  if (distance < 12 || distance > 300) return {reason:'unmatched_route_span'};
  return {ordered,distance};
}

async function readPixels(frame) {
  if (frame.pixels) {
    const {width,height,data,channels} = frame.pixels;
    if (![3,4].includes(channels) || !Number.isInteger(width) || !Number.isInteger(height) ||
        width<96 || height<54 || width*height>12000000 || data?.length!==width*height*channels) throw Error('Invalid pixels');
    return {width,height,at:(x,y)=>Array.from(data.subarray((y*width+x)*channels,(y*width+x)*channels+3))};
  }
  if (process.platform!=='darwin' || typeof frame.imagePath!=='string' || !path.isAbsolute(frame.imagePath)) throw Error('Unavailable decoder');
  const meta = await stat(frame.imagePath);
  if (!meta.isFile() || meta.size<512 || meta.size>8*1024*1024) throw Error('Outside image budget');
  const dir = await mkdtemp(path.join(tmpdir(),'road-colors-'));
  try {
    const output = path.join(dir,'pixels.bmp');
    await run('/usr/bin/sips',['--matchTo','/System/Library/ColorSync/Profiles/sRGB Profile.icc','--resampleWidth','512',
      '-s','format','bmp',frame.imagePath,'--out',output],{timeout:6000,maxBuffer:32768});
    const bmp = await readFile(output);
    if (bmp.length < 54 || bmp.toString('ascii',0,2)!=='BM' || bmp.readUInt32LE(30)!==0) throw Error('Invalid decoded image');
    const offset=bmp.readUInt32LE(10), width=bmp.readInt32LE(18), signedHeight=bmp.readInt32LE(22);
    const height=Math.abs(signedHeight), bytes=bmp.readUInt16LE(28)/8, stride=Math.ceil(width*bytes/4)*4;
    if (width!==512 || height<128 || height>550 || ![3,4].includes(bytes) || offset+stride*height>bmp.length) throw Error('Unsupported image');
    return {width,height,at:(x,y)=>{
      const i=offset+(signedHeight<0?y:height-1-y)*stride+x*bytes;
      return [bmp[i+2],bmp[i+1],bmp[i]];
    }};
  } finally {
    await rm(dir,{recursive:true,force:true});
  }
}

function frameColors(image) {
  const {width,height} = image;
  if (width/height < 1.22 || width/height > 2.24) return {reason:'unsupported_orientation'};
  const pixel=(x,y)=>image.at(clamp(Math.floor(x*width),0,width-1),clamp(Math.floor(y*height),0,height-1));
  function patch(x,y,dx,dy,n=7) {
    const rgb=[];
    for (let j=0;j<n;j++) for (let i=0;i<n;i++)
      rgb.push(pixel(x+dx*((i+.5)/n-.5),y+dy*((j+.5)/n-.5)));
    return rgb;
  }
  const sky = patch(.5,.17,.88,.26,19);
  const skyL = sky.map(lumen);
  const daylight = sky.filter(([r,g,b])=> b>108 && g>88 && b>=.97*g && b>=.99*r).length / sky.length;
  const blueDaylight = sky.filter(([r,g,b])=>g>82 && b>129 && b>r*1.45 && b>g*1.18).length / sky.length;
  if ((percentile(skyL,.80)<117 && blueDaylight<.28) || daylight<.095 || skyL.filter(v=>v>249).length/skyL.length>.83)
    return {reason:'uncertain_daylight_or_orientation'};
  // Widespread small dark marks against smooth overcast sky are grounds to abstain. The
  // scene need not hold them stationary: drops can move between images. Sparse overhead
  // wires and cloud edges are confined to fewer cells or fail the smooth-neighborhood check.
  const zones=new Array(8).fill(0);
  let possible=0, smooth=0, specks=0;
  const offsets=[[-1,-1],[0,-1],[1,-1],[-1,0],[1,0],[-1,1],[0,1],[1,1]];
  for(let j=0;j<32;j++) for(let i=0;i<100;i++){
    const x=.035+i*.0093,y=.052+j*.0084;
    const px=clamp(Math.floor(x*width),5,width-6),py=clamp(Math.floor(y*height),5,height-6),here=image.at(px,py);
    possible++;
    if(here[2]<here[0]*.97 || here[2]>here[0]*1.33)continue;
    const coarse=offsets.map(([dx,dy])=>lumen(image.at(px+dx*5,py+dy*5)));
    const bg=percentile(coarse);
    if(bg<132 || bg>247 || percentile(coarse,.875)-percentile(coarse,.125)>10)continue;
    smooth++;
    const near=offsets.map(([dx,dy])=>lumen(image.at(px+dx*2,py+dy*2)));
    if(percentile(near)-lumen(here)>5){
      specks++;
      zones[Math.min(7,Math.floor(i/12.5))]++;
    }
  }
  const dampGlass=smooth/possible>.43 && specks/smooth>.013 && zones.filter(n=>n>=2).length>=4;
  // Find a supported road trapezoid across plausible dashboard/cyclist camera heights.
  // The deepest candidate stops well above the lower bodywork of a typical camera car.
  const rows=[];
  for (let row=0;row<5;row++) {
    const line={cells:[],left:[],right:[],highlight:0,blue:0,pixels:0,contrast:0};
    const y=.57+row*.054, half=.099+row*.018;
    for (let col=-2;col<=2;col++) {
      const points=patch(.50+half*col/2,y,.058,.049);
      const l=points.map(lumen), contrast=percentile(l,.82)-percentile(l,.18);
      line.highlight+=l.filter(x=>x>215).length;
      line.blue+=points.filter(([r,g,b])=>b>r*1.16 && b>g*1.06 && b>125).length;
      line.pixels+=points.length;
      if (contrast>51) line.contrast++;
      const neutral=points.filter(rgb=>{
        const light=lumen(rgb);
        return light>38 && light<216 && span(rgb)<=Math.max(21,light*.23);
      });
      if (neutral.length<points.length*.65 || contrast>39) continue;
      const color=center(neutral);
      line.cells.push(color);
      if (col<0)line.left.push(color);
      if (col>0)line.right.push(color);
    }
    rows.push(line);
  }
  let road=null,support=0,reason='road_obstructed_or_unconfirmed';
  for(let offset=0;offset<=2;offset++){
    const bands=rows.slice(offset,offset+3),cells=bands.flatMap(b=>b.cells);
    const left=bands.flatMap(b=>b.left),right=bands.flatMap(b=>b.right);
    const examined=bands.reduce((a,b)=>a+b.pixels,0);
    const blue=bands.reduce((a,b)=>a+b.blue,0)/examined;
    const highlight=bands.reduce((a,b)=>a+b.highlight,0)/examined;
    const contrast=bands.reduce((a,b)=>a+b.contrast,0);
    if (blue>.10 || (highlight>.115 && contrast>=3) || highlight>.60) {
      reason='possible_wet_or_glare';
      continue;
    }
    if (cells.length<8 || bands.some(b=>b.cells.length<2) || left.length<3 || right.length<3)continue;
    const color=center(cells), roadY=lumen(color);
    if (roadY<45 || roadY>211 || difference(center(left),center(right))>28 ||
        difference(center(bands[0].cells),center(bands[2].cells))>38 ||
        percentile(cells.map(v=>difference(v,color)),.75)>25){
      reason='inconsistent_road_exposure';
      continue;
    }
    if(cells.length>support){road=color;support=cells.length;}
  }
  if(!road)return {reason};
  // Accept only spatially textured green cover. Both sides must contribute to a tile-wide vegetation color.
  const sides=[[],[]];
  for (let side=0;side<2;side++) for (let row=0;row<4;row++) for (let col=0;col<4;col++) {
    const x=.046+col*.063, y=.29+row*.079;
    const colors=patch(side?1.-x:x,y,.055,.06);
    const green=colors.filter(([r,g,b])=>g>r*1.035 && g>b*1.18 && g-r>=3 && g-b>=8 && g>28 && g<177);
    if (green.length<colors.length*.63) continue;
    const light=green.map(lumen);
    if (percentile(light,.82)-percentile(light,.18)<7 || percentile(light,.86)-percentile(light,.14)>90) continue;
    sides[side].push(center(green));
  }
  let vegetation=null;
  if (sides.every(side=>side.length>=2)) {
    const lr=sides.map(center);
    if (difference(...lr)<39) vegetation={color:center(lr),support:sides[0].length+sides[1].length};
  }
  // Retain clear sky only when smooth blue patches span the center and sides
  // of the upper field of view in both bands. Cloud/time observations elsewhere
  // cannot establish a sky color for this camera direction.
  function clearSkyBand(y) {
    const accepted=[];
    for (let i=0;i<7;i++) {
      const patchRgb=patch(.23+i*.09,y,.065,.055);
      const color=center(patchRgb), [r,g,b]=color;
      if(b<102 || g<60 || b<g+10 || b<r+25 || g<r*.98)continue;
      if(percentile(patchRgb.map(v=>difference(v,color)),.90)>8)continue;
      accepted.push({col:i,color});
    }
    if(accepted.length<4 || !accepted.some(p=>p.col<3) || !accepted.some(p=>p.col>3))return null;
    return {color:center(accepted.map(p=>p.color)),support:accepted.length};
  }
  return {road:{color:road,support},vegetation,dampGlass,skyUpper:clearSkyBand(.11),skyLower:clearSkyBand(.28)};
}

function aggregate(rows, kind) {
  const evidence=rows.map(row=>row[kind]).filter(Boolean);
  if (evidence.length < 3) return null;
  const color=center(evidence.map(v=>v.color));
  const spread=Math.max(...evidence.map(v=>difference(v.color,color)));
  const threshold=kind==='road'?24:31;
  if (spread>threshold) return null;
  const srgb=color.map(v=>round(v/255));
  const coverage=Math.min(1,evidence.reduce((a,b)=>a+b.support,0)/(evidence.length*(kind==='road'?14:12)));
  const confidence=round(clamp(.55+.14*coverage+.12*(1-spread/threshold),.50,.81));
  return {srgb,linear:srgb.map(v=>round(srgbToLinear(v))),confidence,samples:evidence.length};
}

export async function estimateStreetAppearance(frames) {
  const list=Array.isArray(frames)?frames:[];
  const result={version:1,status:'insufficient',reason:'insufficient_distinct_photos',inputCount:list.length,acceptedFrames:0,
    sequenceId:null,distanceMeters:0,confidence:0,road:null,vegetation:null};
  if (list.length<3) return result;
  if (list.length>6 || list.some(f=>!f || typeof f!=='object')) return {...result,status:'rejected',reason:'invalid_input'};
  const route=routeGeometry(list);
  if (route.reason) return {...result,status:'rejected',reason:route.reason};
  result.sequenceId=String(list[0].sequenceId);result.distanceMeters=Math.round(route.distance);
  const evidence=await Promise.all(route.ordered.map(async f=>{
    try {return frameColors(await readPixels(f));} catch {return {reason:'unavailable_or_invalid_image'};}
  }));
  result.acceptedFrames=evidence.filter(e=>e.road).length;
  if (evidence.some(e=>!e.road)) return {...result,status:'rejected',reason:evidence.find(e=>!e.road).reason};
  if (evidence.filter(e=>e.dampGlass).length>=2)
    return {...result,status:'rejected',reason:'obstructed_camera_glass'};
  const road=aggregate(evidence,'road');
  if (!road) return {...result,status:'rejected',reason:'road_color_disagrees_across_frames'};
  const upper=aggregate(evidence,'skyUpper'),lower=aggregate(evidence,'skyLower');
  let sky=null;
  if(upper && lower && Math.abs(lumen(upper.srgb)-lumen(lower.srgb))<.28){
    const east=route.ordered.reduce((sum,f)=>sum+Math.sin(f.heading*degrees),0);
    const north=route.ordered.reduce((sum,f)=>sum+Math.cos(f.heading*degrees),0);
    sky={upper:upper.linear,lower:lower.linear,heading:(Math.atan2(east,north)/degrees+360)%360,confidence:Math.min(upper.confidence,lower.confidence)};
  }
  return {...result,status:'accepted',reason:'matched_daylight_sequence',confidence:road.confidence,road,vegetation:aggregate(evidence,'vegetation'),clearSky:sky};
}
