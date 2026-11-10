// Paved road widths, from the vector tile alone.
//
// A road's width is a function of its OpenMapTiles class, one-way flag and
// ramp flag, which come from the OSM way itself, so both tiles of every seam
// (and both zoom-14 parents of a road crossing their boundary) always give the
// same road the same width, whatever any network source answered when either
// tile was built. (An earlier draft read OSM lane counts from Esri's OSM
// replica per zoom-14 tile; a tile built while that query failed got other
// widths than its neighbours, so seams stopped chaining.)
//
// Two-way roads keep the class reference widths below, an average over the lane
// counts of their class. A one-way carriageway (half of a divided highway, one
// street of a couplet) is two 3.6 m lanes plus an outside and a median shoulder
// by class instead of the full two-way width: every trunk used to be 16 m, so a
// divided highway whose carriageways are 12-16 m apart became two overlapping
// 16 m strips (Vidalia GA, US-280) and a couplet split overlapped its own
// approach (Eureka CA, US-101). A ramp is one lane with ramp shoulders.
//
// Every width here is at most the class reference width. Surveyed data bundled
// with the game (lidar crossfall, the Needles outcrop ground, lane layouts) is
// keyed on a road signature that signs the reference width of each road's class
// (SurveyOutcropGround.gd road_signature, road-layout.mjs laneRoadSignature), so
// it stays valid, and a survey made for the reference width still covers the
// narrower paved road.
export const LANE_M=3.6;
export const referenceWidths={motorway:19,trunk:16,primary:12,secondary:11,tertiary:10,minor:8,service:4.6,track:3};
const MAJOR=['motorway','trunk','primary','secondary','tertiary'];
// Paved shoulder per side of a one-way carriageway, metres: [outside, median].
// AASHTO-style rural values; on urban streets they stand in for the gutter and
// parking edge.
const SHOULDER={motorway:[3,1.2],trunk:[2.4,.9],primary:[1.8,.6],secondary:[1.5,.6],tertiary:[1.2,.6]};
const RAMP_SHOULDER=[1.8,.6];
const ONE_WAY_LANES=2,RAMP_LANES=1;

export function roadWidth(cls,oneway=0,ramp=0){
 if(!MAJOR.includes(cls))return referenceWidths[cls];
 const isRamp=Number(ramp??0)===1;
 if(!isRamp&&!Number(oneway??0))return referenceWidths[cls];
 const [outside,inside]=isRamp?RAMP_SHOULDER:SHOULDER[cls];
 const width=(isRamp?RAMP_LANES:ONE_WAY_LANES)*LANE_M+outside+inside;
 return Math.min(referenceWidths[cls],Math.round(width*100)/100);
}

// Width table: `widths[cls]` is the class reference width (defined for every
// class that is drawn as a road) and `widths.of(feature)` the width of one
// vector-tile road.
export const roadWidths=Object.defineProperty({...referenceWidths},'of',{
 value:feature=>{const p=feature.properties;return roadWidth(p.class,p.oneway,p.ramp)}
});
