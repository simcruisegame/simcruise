// Run with bun lab/check-pedestrian.mjs with the ordinary atlas dependencies available.
import assert from 'node:assert/strict';
import {pedestrianSurfaces} from '../helper/pedestrian.mjs';

const p=(x,y)=>({x,y});
const line=[p(0,0.5),p(1,0.5)];
const square=(l,r)=>[p(l,l),p(r,l),p(r,r),p(l,r),p(l,l)];
const ring=square(.25,.75),court=square(.4,.6).reverse();
const building={type:3,properties:{},loadGeometry:()=>[ring,court]};
const make=(tags,geometry=line)=>({type:2,properties:{class:'path',subclass:'footway',surface:'paved',...tags},loadGeometry:()=>[geometry]});
const roads=[make({}),make({brunnel:'bridge'}),make({brunnel:'tunnel'}),make({layer:1}),make({level:-1}),make({indoor:'yes'}),make({surface:'unpaved'}),make({surface:'',subclass:'pedestrian'}),make({subclass:'steps'}),make({subclass:'corridor'}),make({class:'minor'}),make({surface:'',footway:'sidewalk'},[p(.05,.05),p(.1,.1)])];
const each=(name,accept)=>{for(const feature of (name==='building'?[building]:roads))accept(feature,pt=>[pt.x,pt.y]);};
assert.deepEqual(pedestrianSurfaces(each,(a,b)=>[a,b]),[
 ['footway',1.8,[[0,.5],[.25,.5]]],
 ['footway',1.8,[[.4,.5],[.6,.5]]],
 ['footway',1.8,[[.75,.5],[1,.5]]],
 ['sidewalk',1.8,[[.05,.05],[.1,.1]]],
]);
console.log('PASS pedestrian filtering, exterior segment clipping, courtyard and explicit sidewalk');
