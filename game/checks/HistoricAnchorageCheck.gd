extends SceneTree

const ERA=preload("res://HistoricAnchorage.gd")

func _initialize():
 var rejected=[[],["--reference-epoch","1432331510"],["--source-era"],["--source-era","2007-07-19"],["--source-era","2015-05-23"],["--source-era","2024-07-01"],["--source-era","2014-02-29"],["--source-era","2013-02-31"],["--source-era","2013-13-01"],["--source-era","2013-0x-04"],["--source-era","2013-04-3x"],["--source-era","2015-05-22","--reference-epoch","0"],["--source-era","2015-05-22","--reference-epoch","1432417910"],["--source-era","2015-05-22","--source-era","2014-10-22"]]
 var accepted=[["--source-era","2007-07-20"],["--source-era","2012-02-29"],["--source-era","2015-05-22","--reference-epoch","1432331510"]]
 for i in rejected:
  if ERA.enabled(PackedStringArray(i)):push_error("Unexpected source-era acceptance "+str(i));quit(1);return
 for i in accepted:
  if not ERA.enabled(PackedStringArray(i)):push_error("Unexpected source-era refusal "+str(i));quit(2);return
 # Before local midnight in summer and winter, the UTC day is one day ahead.
 for sample in [["2013-08-03","2013-08-04T07:59:59"],["2013-12-25","2013-12-26T08:59:59"]]:
  var t=int(Time.get_unix_time_from_datetime_string(sample[1]))
  if not ERA.enabled(PackedStringArray(["--source-era",sample[0],"--reference-epoch",str(t)])) or ERA.enabled(PackedStringArray(["--source-era",sample[0],"--reference-epoch",str(t+1)])):
   push_error("Anchorage midnight "+str(sample));quit(3);return
 var ring=[]
 for p in ERA.RING:ring.append([p[0]*.00001,p[1]*.00001])
 var b=[[ring],[],5.,0.]
 if not ERA.matches([5480,18579],b,{"eave_m":39.2}) or ERA.matches([5481,18579],b,{"eave_m":39.2}) or ERA.matches([5480,18579],b,{"eave_m":32.91}):
  push_error("Survey scope mismatch");quit(4);return
 ring[0][0]+=.001
 if ERA.matches([5480,18579],b,{"eave_m":39.2}):push_error("Stale footprint accepted");quit(5);return
 print("Historic Anchorage passed source-era dates, reference timestamps, both local midnight offsets, tile, roof and exact footprint.")
 quit()
