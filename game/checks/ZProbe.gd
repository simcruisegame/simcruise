extends SceneTree
# Scratch handling probe (not committed). --test=ss|step|sine|tap|straight
var car
var test="ss"
var tick=0
var t=0.
var phase=0
var V=26.8224
var speeds=[]
var si=0
var trial_list=[]
var ti=0
var rack=0.
var prev_vel=Vector3.ZERO
var integ=0.
var log_rows=[]
var acc_rows=[]
var setv={}
func arg(name,def):
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--"+name+"="):return a.split("=")[1]
 return def
func _initialize():
 test=arg("test","ss")
 Engine.physics_ticks_per_second=int(arg("tps","60"))
 var slab=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(6000,1,6000);col.shape=box;slab.position.y=-.5;slab.add_child(col);root.add_child(slab)
 car=load("res://Powertrain.gd").new()
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--set="):
   var kv=a.substr(6).split("=");var cur=car.spec.get(kv[0])
   var val=str_to_var(kv[1])
   car.spec.set(kv[0],val)
 car.use_spec(car.spec)
 car.setup();root.add_child(car)
 car.steering_assist=(test=="tap")
 if test=="ss":
  for v in [13.4,20.,26.8,33.5]:trial_list.append({"v":v})
 elif test=="step":
  for v in [17.9,26.8,31.3]:
   for g in [.3,.6]:trial_list.append({"v":v,"g":g})
 elif test=="sine":
  for f in [.2,.4,.7,1.,1.5,2.,3.]:trial_list.append({"v":26.8,"f":f})
 elif test=="tap":
  for v in [17.9,26.8,31.3]:
   for d in [.05,.1,.2,.3,.5]:trial_list.append({"v":v,"d":d})
 elif test=="straight":
  trial_list.append({"v":26.8})
 begin()
func begin():
 if ti>=trial_list.size():quit(0);return
 car.restart(Vector3(0,.18,0),0.)
 tick=0;t=0.;phase=0;rack=0.;integ=0.;acc_rows=[]
 V=trial_list[ti].v
func fwd_speed()->float:return -car.linear_velocity.dot(car.global_transform.basis.z)
func lat_acc(dt):
 var a=(car.linear_velocity-prev_vel)/dt
 return -a.dot(car.global_transform.basis.x)  # + = left
func yawdeg():
 var f=-car.global_transform.basis.z;return rad_to_deg(atan2(-f.x,-f.z))
func beta():
 var b=car.global_transform.basis;return rad_to_deg(atan2(-car.linear_velocity.dot(b.x),-car.linear_velocity.dot(b.z)))
func _physics_process(dt):
 tick+=1;t+=dt
 var speed=fwd_speed()
 var gas=0.;var brake=0.
 if phase==0:
  if tick==30:
   car.linear_velocity=-car.global_transform.basis.z*V;car.omega.fill(V/car.R)
  if tick>30 and t>1.5:phase=1;t=0.
 # speed hold PI
 var err=V-speed
 integ=clampf(integ+err*dt*.3,-1.,1.)
 gas=clampf(.25+err*.8+integ,0.,1.)
 if err< -1.5:gas=0.;brake=clampf(-err*.05,0.,.2)
 var turn=0.
 var tr=trial_list[ti]
 var ay=lat_acc(dt)
 if phase>=1:
  if test=="tap":
   turn=0.
   if t>=1. and t<1.+tr.d:turn=1.
  else:
   rack=manoeuvre(tr,t,dt)
   var cap=car.available_steering(speed,false)
   car.steering_input=rack/cap;turn=rack/cap
 var spd=car.drive(dt,gas,brake,turn,false)
 prev_vel=car.linear_velocity
 if phase>=1:
  acc_rows.append([t,rad_to_deg(car.steering),rad_to_deg(car.angular_velocity.y),ay/9.80665,beta(),yawdeg(),car.global_position.x,speed,rad_to_deg(car.rolling_slip[0]),rad_to_deg(car.rolling_slip[2]),rad_to_deg(car.global_transform.basis.get_euler().z)])
  if finished(tr,t):
   report(tr);ti+=1;begin()
 return false
func manoeuvre(tr,tt,dt)->float:
 var L=car.WHEELBASE_M
 if test=="ss":
  # stair: step every 4 s, rack for 0.1 g increments of neutral * 1.4
  var k=int(tt/4.)+1
  return L*(.1*k*9.80665)/(tr.v*tr.v)*1.4
 if test=="step":
  var target=L*tr.g*9.80665/(tr.v*tr.v)*float(tr.get("k",1.6))
  return target*clampf((tt-1.)/.1,0.,1.)
 if test=="sine":
  var amp=L*.2*9.80665/(tr.v*tr.v)*1.4
  return amp*sin(TAU*tr.f*(tt-1.)) if tt>1. else 0.
 return 0.
func finished(tr,tt)->bool:
 if test=="ss":return tt>=4.*9 or absf(acc_rows.back()[4])>8.
 if test=="step":return tt>=5.
 if test=="sine":return tt>=1.+maxf(5./tr.f,4.)
 if test=="tap":return tt>=6.
 if test=="straight":return tt>=10.
 return true
func mean(rows,col,a,b):
 var s=0.;var n=0
 for r in rows:
  if r[0]>=a and r[0]<b:s+=r[col];n+=1
 return s/maxf(n,1)
func report(tr):
 var R=acc_rows
 if test=="ss":
  var pts=[]
  for k in range(1,10):
   var a=4.*k-1.5;var b=4.*k
   if a>R.back()[0]:break
   pts.append([mean(R,1,a,b),mean(R,2,a,b),mean(R,3,a,b),mean(R,4,a,b),mean(R,8,a,b),mean(R,9,a,b),mean(R,10,a,b)])
  print("SS v=%.1f m/s (%.0f mph)  neutral yaw gain V/L=%.2f 1/s"%[tr.v,tr.v*2.23694,tr.v/car.WHEELBASE_M])
  for p in pts:
   print("  rack=%.3f deg  yaw=%.2f deg/s  ay=%.3f g  beta=%.2f  aF=%.2f aR=%.2f roll=%.2f  r/d=%.2f 1/s  hw(15:1)=%.1f deg"%[p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[1]/maxf(p[0],1e-6),p[0]*15.])
  # fit K on ay<=0.45
  var sx=0.;var sy=0.;var sxx=0.;var sxy=0.;var n=0
  for p in pts:
   if absf(p[2])<=.45:sx+=p[2];sy+=p[0];sxx+=p[2]*p[2];sxy+=p[2]*p[0];n+=1
  if n>=2:
   var slope=(n*sxy-sx*sy)/(n*sxx-sx*sx)
   var ack=rad_to_deg(car.WHEELBASE_M*9.80665/(tr.v*tr.v))
   var K=slope-ack
   var vchar=sqrt(57.2958*car.WHEELBASE_M*9.80665/maxf(K,1e-4)) if K>0 else -1.
   print("  FIT dRack/dAy=%.3f deg/g  ackermann=%.3f  K=%.3f deg/g (rack)  =%.2f deg/g at 15:1 hw  vchar=%.1f m/s"%[slope,ack,K,K*15.,vchar])
 elif test=="step":
  var r_ss=mean(R,2,4.,5.);var ay_ss=mean(R,3,4.,5.)
  var t50=1.05;var t90r=-1.;var tpk=-1.;var rpk=0.;var t90a=-1.
  for r in R:
   if absf(r[2])>absf(rpk):rpk=r[2];tpk=r[0]
   if t90r<0 and absf(r[2])>=.9*absf(r_ss):t90r=r[0]
   if t90a<0 and absf(r[3])>=.9*absf(ay_ss):t90a=r[0]
  print("STEP v=%.1f target=%.1fg rack=%.2f deg: yaw_ss=%.2f ay_ss=%.3f  T90_yaw=%.3f s  Tpk_yaw=%.3f s  overshoot=%.1f%%  T90_ay=%.3f s  beta_ss=%.2f"%[tr.v,tr.g,R.back()[1],r_ss,ay_ss,t90r-t50,tpk-t50,100.*(rpk/r_ss-1.),t90a-t50,mean(R,4,4.,5.)])
 elif test=="sine":
  var f=tr.f;var a0=1.+1./f;var a1=1.+maxf(5./f,4.)
  a0=a1-floor((a1-a0)*f)/f
  var c={}
  for col in [1,2,3]:
   var sr=0.;var cr=0.;var n=0
   for r in R:
    if r[0]>=a0 and r[0]<a1:sr+=r[col]*sin(TAU*f*(r[0]-1.));cr+=r[col]*cos(TAU*f*(r[0]-1.));n+=1
   c[col]=Vector2(sr,cr)*2./n
  var d=c[1];var y=c[2];var a=c[3]
  var gy=y.length()/d.length();var py=rad_to_deg(atan2(y.y,y.x)-atan2(d.y,d.x))
  var ga=a.length()/d.length();var pa=rad_to_deg(atan2(a.y,a.x)-atan2(d.y,d.x))
  print("SINE f=%.2f Hz rack_amp=%.3f deg  yaw gain=%.2f (deg/s)/deg  phase=%.1f deg   ay gain=%.3f g/deg  phase=%.1f deg"%[f,d.length(),gy,py,ga,pa])
 elif test=="tap":
  var pk_rack=0.;var pk_yaw=0.;var pk_ay=0.;var neg_yaw=0.;var pk_beta=0.
  for r in R:
   pk_rack=maxf(pk_rack,absf(r[1]));pk_yaw=maxf(pk_yaw,r[2]);pk_ay=maxf(pk_ay,r[3]);pk_beta=maxf(pk_beta,absf(r[4]))
   if r[0]>1.+tr.d:neg_yaw=minf(neg_yaw,r[2])
  var h=R.back()[5]-R[0][5];var lat=R.back()[6]-R[0][6]
  print("TAP v=%.0f mph dur=%.2f s: peak rack=%.2f deg (hw %.0f deg @15:1) yaw_pk=%.2f deg/s ay_pk=%.3f g beta_pk=%.2f reverse_yaw=%.2f heading_5s=%.2f deg lat_offset=%.2f m"%[tr.v*2.23694,tr.d,pk_rack,pk_rack*15.,pk_yaw,pk_ay,pk_beta,neg_yaw,h,-lat])
 elif test=="straight":
  var s2=0.;var n=0;var ay2=0.
  for r in R:
   if r[0]>2.:s2+=r[2]*r[2];ay2+=r[3]*r[3];n+=1
  print("STRAIGHT yaw_rms=%.4f deg/s ay_rms=%.4f g heading drift=%.3f deg lateral=%.3f m"%[sqrt(s2/n),sqrt(ay2/n),R.back()[5]-R[0][5],R.back()[6]-R[0][6]])
