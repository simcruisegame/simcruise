"""Run ordinary canopy streaming in an isolated source checkout. No merged runtime test hook."""
import argparse, json, os, shutil, subprocess
from pathlib import Path
from shlex import quote
here=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('scratch',type=Path);parser.add_argument('godot',type=Path);parser.add_argument('bun',type=Path)
parser.add_argument('--cache',type=Path);parser.add_argument('--poses',nargs='+',default=['Near','Held'])
args=parser.parse_args()
root=here.parent.parent; scratch=args.scratch.resolve();game=scratch/'game'; cache=(args.cache or scratch/'cache').resolve()
scratch.mkdir(exist_ok=True,parents=True);cache.mkdir(exist_ok=True,parents=True)
if not game.is_dir():
 for base,ds,fs in os.walk(root/'game'):
  dest=game/Path(base).relative_to(root/'game');dest.mkdir(exist_ok=True)
  for f in fs:
   try:os.link(Path(base)/f,dest/f)
   except OSError:shutil.copy2(Path(base)/f,dest/f)
def patch(relative,match,replacement):
 src=root/'game'/relative;dst=game/relative
 text=src.read_text();assert match in text
 dst.unlink(missing_ok=True);dst.write_text(text.replace(match,replacement,1))
patch('Drive.gd','ProjectSettings.globalize_path("user://map-cache")',json.dumps(str(cache)))
patch('SurveyedTreeWell.gd',' if r.size()!=13',' if not tile.info.get("qa_survey",true):return false\n if r.size()!=13')
patch('Grove.gd','  for record in tile.grove:', '  for record in tile.grove:')
p=game/'Grove.gd';s=p.read_text().replace('if row.size()>6 and row[6] is Dictionary','if get_meta("qa_survey",true) and row.size()>6 and row[6] is Dictionary');p.write_text(s)
patch('WorldTile.gd','func replace_crowns(rows):','var qa_original_crowns:Array=[]\nfunc replace_crowns(rows):\n qa_original_crowns=rows')
p=game/'checks/ReferencePose.gd';s=(root/'game/checks/ReferencePose.gd').read_text()
s=s.replace(' var rec=game.street_detail.records.get(target_tile,{})',' if game.tiles.size()<6 or not active.info.get("canopy_ready",false) or (active.road_prior and not active.learned_edges):return\n var rec=game.street_detail.records.get(target_tile,{})')
s=s.replace(' if "--landscape-cost" in OS.get_cmdline_user_args():',' if "--city-crowns-check" in OS.get_cmdline_user_args():\n  await capture_city_crowns(active)\n  return\n if "--landscape-cost" in OS.get_cmdline_user_args():')
p.unlink(missing_ok=True);p.write_text(s+'\n'+(here/'normal_capture.gdinc').read_text())
tools=scratch/'tools';tools.mkdir(exist_ok=True);cli=tools/'atlas';cli.unlink(missing_ok=True)
cli.write_text('#!/bin/sh\nexec '+quote(str(args.bun))+' '+quote(str(root/'helper'/'atlas.mjs'))+' "$@"\n');cli.chmod(0o755)
poses=json.loads((here/'registration.json').read_text())['poses']
for name,tile,lat,lon,yaw,fov,pitch,date in poses:
 if name not in args.poses:continue
 from datetime import datetime,timezone
 epoch=int(datetime.fromisoformat(date).replace(tzinfo=timezone.utc).timestamp())
 params=[str(args.godot),'--path',str(game),'--position','4400,960','--resolution','960x540','--audio-driver','Dummy','--max-fps','45','--','--reference',str(lat),str(lon),str(yaw),str(fov),str(scratch/(name+'.jpg')),str(pitch),'0','--reference-epoch',str(epoch),'--city-crowns-check']
 with (scratch/(name+'.log')).open('w') as log:subprocess.run(params,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=110)
