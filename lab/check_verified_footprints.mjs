import assert from 'node:assert/strict';
import {footprintCandidates,verifiedFootprints,hasFootprintSurvey} from '../helper/verified-footprints.mjs';
const id='37265c92-a1f3-4136-998c-86a1aa292ca8',ring=[[.3,.3],[.35,.3],[.35,.34],[.3,.34]];
const props={id,has_parts:false,is_underground:false,'@geometry_source':'Microsoft ML Buildings',sources:JSON.stringify([{property:'',dataset:'Microsoft ML Buildings',license:'ODbL-1.0'}])};
const observed={props,rings:[ring],box:[.3,.3,.35,.34]};
const layers={transportation:{length:0},building:{length:0}};
const locate=(data=[observed],l=layers)=>footprintCandidates(0,32768,l,data);
const candidates=locate();assert.equal(candidates.length,1);
const entry={id,outlineHash:candidates[0].outlineHash,geometrySource:'Microsoft ML Buildings',survey:'miami-dade-2021',heightM:5.6,roofReturns:210,groundReturns:97,coveredCells:8,eligibleCells:9};
const accept=(record=entry,data=[observed],l=layers)=>verifiedFootprints(0,32768,l,data,{[id]:record});
assert.equal(accept().length,1);assert.equal(accept()[0].heightM,5.6);
for(const invalid of [{heightM:NaN},{coveredCells:5},{groundReturns:20},{roofReturns:12},{survey:'unknown'},{outlineHash:'other'},{geometrySource:'OpenStreetMap'}])
 assert.equal(accept({...entry,...invalid}).length,0);
assert.equal(hasFootprintSurvey(0,32768),false);
assert.equal(verifiedFootprints(0,32768,layers,[observed]).length,0);
assert.equal(locate([observed,{...observed,props:{...props,id:'00000000-0000-0000-0000-000000000001'}}]).length,0);
assert.equal(locate([{...observed,box:[0,0,.35,.34]}]).length,0);
assert.equal(locate([{...observed,props:{...props,has_parts:true}}]).length,0);
assert.equal(locate([{...observed,props:{...props,is_underground:true}}]).length,0);
assert.equal(locate([{...observed,props:{...props,sources:'[]'}}]).length,0);
function vectorLayer(points,type,properties){
 const geometry=points.map(([x,y])=>({x:x*1024,y:y*1024}));
 return {length:1,feature:()=>({type,properties,extent:4096,loadGeometry:()=>[geometry],
  bbox:()=>[Math.min(...geometry.map(p=>p.x)),Math.min(...geometry.map(p=>p.y)),Math.max(...geometry.map(p=>p.x)),Math.max(...geometry.map(p=>p.y))]})};
}
const road=vectorLayer([[.29,.32],[.4,.32]],2,{class:'minor'});
assert.equal(locate([observed],{...layers,transportation:road}).length,0);
const overlap=vectorLayer([...ring,ring[0]],3,{hide_3d:true});
assert.equal(locate([observed],{...layers,building:overlap}).length,0);
const crossBorder=[[-.1,.27],[.8,.27],[.8,.33],[-.1,.33],[-.1,.27]];
assert.equal(locate([observed],{...layers,building:vectorLayer(crossBorder,3,{})}).length,0);
console.log('Missing footprint certificates, independent lidar evidence, ownership, changed geometry, duplicate, road and hidden-part checks passed.');

const witness={source:'ORNL',validationMethod:'Spatial Intersect',sourceDate:'2024-07-25',iou:.84,centroidM:.9,
 outlineHash:candidates[0].outlineHash,images:[{item:'one',date:'2023-06-20',edges:4,edgeFraction:.9},{item:'two',date:'2020-06-21',edges:3,edgeFraction:.75}]};
const district=(record=witness)=>({release:'test',registration:{pass:true},features:{[id]:record}});
const supplement=(extra={},evidence=district(),base=layers)=>verifiedFootprints(0,32768,base,[{...observed,props:{...props,...extra}}],undefined,evidence);
const unknown=supplement()[0];assert.equal(unknown.heightStatus,'unknown');assert.equal(unknown.displayHeightM,3);
assert.equal(unknown.eavesM,null);assert.equal(Object.hasOwn(unknown,'heightM'),false);
const model=supplement({height:4.39,'@height_source':'Microsoft ML Buildings'})[0];
assert.equal(model.heightStatus,'source-model');assert.equal(model.displayHeightM,4.39);assert.equal(model.eavesM,null);
const foreign={property:'/height',dataset:'another source',license:'ODbL-1.0'};
assert.equal(supplement({height:5,'@height_source':'Microsoft ML Buildings',sources:JSON.stringify([...JSON.parse(props.sources),foreign])})[0].heightStatus,'unknown');
for(const record of [{...witness,source:'External'},{...witness,validationMethod:'Unverified'},{...witness,outlineHash:'stale'},
 {...witness,centroidM:3.1},{...witness,iou:.66},{...witness,images:[witness.images[0],witness.images[0]]},
 {...witness,images:[witness.images[0],{...witness.images[1],date:witness.sourceDate}]}]){
 assert.deepEqual(supplement({},district(record)),[]);
}
assert.deepEqual(supplement({},{...district(),registration:{pass:false}}),[]);
assert.deepEqual(supplement({},district(),{...layers,transportation:road}),[]);
assert.deepEqual(supplement({},district(),{...layers,building:overlap}),[]);
const duplicate=verifiedFootprints(0,32768,layers,[observed],{[id]:entry},district());
assert.equal(duplicate.length,1);assert.equal(duplicate[0].heightM,5.6);assert.equal(duplicate[0].survey,'miami-dade-2021');
console.log('Corroborated outlines, independent photo years, source-owned estimates, unknown height, geometry conflict and measured precedence passed.');
