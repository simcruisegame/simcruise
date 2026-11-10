// Offline checks for bridge decks (helper/road-grade.mjs), from the 2026-11-10
// field test at Moab UT (US-191 over the Colorado River): every tile a deck
// crosses gives it the same height at their seam whichever tile is built first,
// an approach whose bare-earth DEM falls away at the abutment does not dip
// into the deck, and a chain that mixes classes ranks by its highest one.
// Synthetic inputs only; no network.
import assert from 'node:assert/strict';
import {chainProfile,profileAtHeight,bridgeDecks,chainClass,chainTop} from '../helper/road-grade.mjs';

// --- Approach profiles at a bridge end ------------------------------------
// A 200 m approach climbing at 2 % to a bridge at station 200.
const approach=bump=>{
 const pts=[[0,0],[200,0]];
 const parts=[{piece:{pts},s:[0,200],length:200,reversed:false,offset:0}];
 const sample=(x)=>100+.02*x+bump(x);
 return chainProfile(parts,sample,()=>true,0,[false,true]);
};
const grade=(p,a,b)=>(profileAtHeight(p,b)-profileAtHeight(p,a))/(b-a);
// The DEM falls 1.5 m over the last 15 m (a river bank under the abutment):
// the road runs on at its own grade to the deck instead of following it down.
{
 const p=approach(x=>x>185?-(x-185)*.1:0);
 const end=profileAtHeight(p,200);
 assert.ok(Math.abs(end-104)<.15,'end on the approach line, got '+end.toFixed(2));
 for(let s=150;s<200;s+=2){
  const g=grade(p,s,s+2);
  assert.ok(g>0&&g<.04,'grade at '+s+' m is '+(g*100).toFixed(1)+' %');
 }
}
// An embankment climbing 1 m over the last 15 m keeps the higher end (the
// short end fit), as before.
{
 const p=approach(x=>x>185?(x-185)/15:0);
 const end=profileAtHeight(p,200);
 assert.ok(end>104.5,'embankment end kept, got '+end.toFixed(2));
}
// A plain approach is unchanged by the bridge-end rules.
{
 const p=approach(()=>0);
 assert.ok(Math.abs(profileAtHeight(p,200)-104)<.02);
 assert.ok(Math.abs(profileAtHeight(p,100)-102)<.02);
}

// --- Deck heights at a tile seam ------------------------------------------
// One deck from (10, 10) to (90, 10) in parent metres; tile A is x 0-50 and
// tile B x 50-100. Each tile measures the ends differently (its own approach
// fits); A owns the west end, B the east end.
const flat=h=>({S:Float64Array.from([0,10,20,30,40]),Z:Float64Array.from([h,h,h,h,h]),W:Float64Array.from([1,1,1,1,1]),fix:[]});
const terminal=(x,h)=>({x,y:10,profile:flat(h),station:0});
const measured={A:[terminal(10,10),terminal(90,12.4)],B:[terminal(10,9.1),terminal(90,11)]};
const piece={pts:[[10,10],[90,10]],cls:'trunk',name:'Test Bridge',width:10.5,layer:1,oneway:1,bridge:true};
const box={A:{tx0:0,ty0:0,scale:50},B:{tx0:50,ty0:0,scale:50}};
const self={A:[0,0],B:[1,0]};
const worldKey=(x,y)=>x+'-'+y;
const ownerOf=x=>[Math.floor(x/50),0];
const build=async(name,store,nested=false)=>{
 const memo={get:k=>store.get(k),set:(k,z)=>store.set(k,z),
  resolve:nested?null:async(ox)=>{await build(ox===0?'A':'B',store,true)}};
 return bridgeDecks([piece],measured[name],()=>NaN,()=>true,box[name],memo,worldKey,ownerOf,self[name]);
};
for(const order of [['A','B'],['B','A']]){
 const store=new Map();
 const out={};
 for(const name of order)out[name]=await build(name,store);
 const a=out.A[0],b=out.B[0];
 // A's row ends at its east edge (x = 1 in tile units), B's starts at its west edge.
 const ha=a[4][a[2].findIndex(p=>p[0]>=1-1e-6)],hb=b[4][b[2].findIndex(p=>p[0]<=1e-6)];
 assert.equal(ha,hb,'seam heights '+ha+' / '+hb+' building '+order.join(' then '));
 assert.equal(ha,10.5,'owners measured both ends (west 10 by A, east 11 by B)');
 assert.equal(store.get('10-10'),10);assert.equal(store.get('90-10'),11);
}
// Without an owner rule (no ownerOf) a tile measures every end itself, as
// before: the two tiles then disagree at the seam, which is the defect.
{
 const plain=await bridgeDecks([piece],measured.A,()=>NaN,()=>true,box.A,null,worldKey);
 const other=await bridgeDecks([piece],measured.B,()=>NaN,()=>true,box.B,null,worldKey);
 const ha=plain[0][4][plain[0][2].findIndex(p=>p[0]>=1-1e-6)],hb=other[0][4][other[0][2].findIndex(p=>p[0]<=1e-6)];
 assert.notEqual(ha,hb);
}
// --- Chain classes ----------------------------------------------------------
// A chain of 9 m of minor street then 80 m of primary road (Jerome AZ, 1st
// Avenue into Hull Avenue) is a primary chain; one with only a short primary
// part keeps its majority class but still ranks (top) as primary, so the
// through road is built first and lesser streets join onto it.
{
 const part=(cls,length)=>({piece:{cls},length});
 assert.equal(chainClass([part('minor',9),part('primary',80)]),'primary');
 assert.equal(chainClass([part('minor',60),part('minor',40),part('primary',30)]),'minor');
 assert.equal(chainTop([part('minor',60),part('minor',40),part('primary',30)]),'primary');
 assert.equal(chainClass([part('tertiary',10),part('trunk',10)]),'tertiary');   // ties to the first part
 assert.equal(chainTop([part('service',50),part('track',50)]),'service');
}
console.log('bridge deck checks passed');
