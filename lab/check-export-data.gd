extends SceneTree
## Run the matching Godot editor with --headless --main-pack /path/to/export.pck --script /absolute/path/to/this/file.
func _initialize():
 var count:=0
 for name in ["survey-wall-iowa.png","survey-wall-minneapolis.png","needles-granite-detail.png","measured-section-19827-24228.png","day-clear.png","day-cloud-rank.png","day-scattering.png","forest-needle-fan.png","mineral-ground.png","survey-front-13686-24537.png","survey-depth-13686-24537.png"]:
  var path="res://data/"+name
  if not FileAccess.file_exists(path):
   print("EXPORT CHECK FAILED missing raw asset ",name)
   quit(1)
   return
  var bytes=FileAccess.get_file_as_bytes(path)
  var image=Image.new()
  if bytes.is_empty() or image.load_png_from_buffer(bytes)!=OK or image.get_size().x<16:
   print("EXPORT CHECK FAILED image ",name)
   quit(1)
   return
  print(name," ",image.get_size()," ",bytes.size()," bytes")
  count+=1
 for name in ["lidar-roofs-5480-18579.json","survey-wall-15788-23579.json","survey-wall-16104-24411.json","als-forest-index.json","als-forest-1578-2986.json","als-ground-1578-2986.meshdata","als-far-crown-leaf-2.meshdata","als-far-forest-conifer-2.meshdata","needles-granite.meshdata","needles-granite-a.meshdata","needles-granite-b.meshdata","needles-ground-13918.meshdata","needles-ground-13919.meshdata"]:
  var path="res://data/"+name
  if not FileAccess.file_exists(path) or FileAccess.get_file_as_bytes(path).size()<90:
   print("EXPORT CHECK FAILED airborne survey ",name)
   quit(1)
   return
  count+=1
 print("EXPORT ASSETS PASSED ",count)
 quit()
