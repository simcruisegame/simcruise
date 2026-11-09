extends RefCounted
# Logistic regression trained on orthophotos, county road polygons, and the
# centreline prior at comparable sampling resolution. See lab/learn_edges.py.
const WEIGHTS=[-7.19292772,8.27682093,-13.0601081,-4.22661441,6.47164131,-1.12842714,-.45454173]
const N=256
static func geometric_prior(roads,side):
 var svg=PackedStringArray(['<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256"><rect width="256" height="256" fill="black"/>'])
 var lines=[]
 for road in roads:
  if road[0]=="track":continue
  var points=PackedStringArray()
  for p in road[3]:points.append("%.2f,%.2f"%[p[0]*N,p[1]*N])
  lines.append([" ".join(points),road[4]])
 for step in [[7.,30],[4.,70],[2.,100],[0.,155],[-2.,190],[-4.,225],[-6.,249]]:
  for line in lines:
   var width=(line[1]+step[0])*N/side
   if width<=0:continue
   var gray="%02x"%step[1]
   svg.append('<polyline fill="none" stroke="#%s" stroke-width="%.2f" stroke-linejoin="round" stroke-linecap="round" points="%s"/>'%[gray+gray+gray,width,line[0]])
 svg.append("</svg>")
 var im=Image.new()
 if im.load_svg_from_string("".join(svg))!=OK:return null
 im.convert(Image.FORMAT_R8)
 return im
static func classify(image:Image,prior:Image):
 var rgb=image.get_data()
 var mask=prior.get_data()
 var answer=PackedByteArray()
 answer.resize(N*N*2)
 for i in mask.size():
  var p=mask[i]/255.;answer[2*i+1]=mask[i]
  if p<.03 or p>.94:
   answer[2*i]=255 if p>.94 else 0
   continue
  var r=rgb[i*3]/255.;var g=rgb[i*3+1]/255.;var b=rgb[i*3+2]/255.
  var lum=(r+g+b)/3.;var mean=0.
  var x=i%N;var y=floori(float(i)/N)
  for dy in [-1,0,1]:
   for dx in [-1,0,1]:
    var j=(clampi(y+dy,0,N-1)*N+clampi(x+dx,0,N-1))*3
    mean+=(rgb[j]+rgb[j+1]+rgb[j+2])/(255.*27.)
  var score=WEIGHTS[0]+WEIGHTS[1]*p+WEIGHTS[2]*(r-g)+WEIGHTS[3]*(g-b)+WEIGHTS[4]*lum+WEIGHTS[5]*absf(lum-mean)+WEIGHTS[6]*(absf(r-g)+absf(g-b))
  answer[2*i]=roundi(255./(1.+exp(-score)))
 return Image.create_from_data(N,N,false,Image.FORMAT_RG8,answer)
