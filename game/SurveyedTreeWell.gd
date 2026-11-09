extends RefCounted
# Cambridge's GIS trunk is inside a separately surveyed occupied sidewalk well.
# Its DBH and a buffer clear the city pavement. Guard the game's travel lanes and buildings too.
static func permits(tile,p:Vector2,r:Array)->bool:
 if r.size()!=13 or r[10]!="matched" or r[11]!="cambridge-ma" or not r[12] is Dictionary:return false
 var well=r[12].get("treeWell")
 if not well is Dictionary or well.get("source")!="cambridge-planimetric-2018" or tile.occupied(p):return false
 var radius=float(r[4])*.5
 if float(well.get("pavementClearanceM",0.))+.011<.32+maxf(.24,radius):return false
 var width=float(well.get("widthM",0.))
 if width<5.5 or width>9.6:return false
 var hit=tile.nearest(p)
 if hit.is_empty() or hit[1][3]>11.1 or hit[1][4]!=0 or hit[2]<=3.20+radius:return false
 var normal=Vector2(float(well.get("normalEast",0.)),-float(well.get("normalNorth",0.)))
 var tangent=Vector2(hit[1][1]-hit[1][0]).normalized()
 return absf(normal.length()-1.)<.005 and absf(normal.dot(tangent))<.22 and (hit[0]-p).normalized().dot(normal)>.9
