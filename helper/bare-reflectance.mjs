// Reconstruct L2A surface reflectance from an already fetched true-colour
// image. The ground label and the image must come from the same acquisition.
export function bareReflectance(rgb,classes,width,height) {
 const size=width*height,output=new Uint8Array(size*4);
 const rho=dn=>(dn-1)*.25/254;
 let measured=0,estimated=0;
 const slopes=new Float32Array(size);
 for(let i=0;i<size;i++) {
  const [r,g,b]=rgb.subarray(i*4,i*4+3);
  if(classes[i]!==5||Math.min(r,g,b)<=1||Math.max(r,g,b)>=254)continue;
  const p=i*4;
  for(let k=0;k<3;k++)output[p+k]=Math.round(510*rho(rgb[p+k]));
  output[p+3]=255;measured++;
  if(r>g && g-b>=12)slopes[i]=(r-g)/(g-b);
 }
 for(let y=0;y<height;y++)for(let x=0;x<width;x++) {
  const i=y*width+x,p=i*4;
  const r=rgb[p],g=rgb[p+1],b=rgb[p+2];
  if(classes[i]!==5||r<254||g>=254||b<=1||g-b<12)continue;
  const support=[];
  for(let sy=Math.max(0,y-5);sy<=Math.min(height-1,y+5);sy++) {
   for(let sx=Math.max(0,x-5);sx<=Math.min(width-1,x+5);sx++) {
    const j=sy*width+sx,s=slopes[j];
    if(!s||s>3.4||Math.abs(rgb[j*4+1]-g)>60)continue;
    if(Math.abs((rgb[j*4+1]-rgb[j*4+2])/rgb[j*4+1]-(g-b)/g)>.12)continue;
    support.push(s);
   }
  }
  if(support.length<5)continue;
  support.sort((a,b)=>a-b);
  const mid=support[Math.floor(support.length/2)];
  if(support[Math.floor(support.length*.75)]-support[Math.floor(support.length*.25)]>.72)continue;
  const red=rho(g)+mid*(rho(g)-rho(b));
  if(red<.237||red>.39)continue;
  output[p]=Math.round(510*Math.max(.25,red));output[p+1]=Math.round(510*rho(g));
  output[p+2]=Math.round(510*rho(b));output[p+3]=224;estimated++;
 }
 return {output,measured,estimated};
}
