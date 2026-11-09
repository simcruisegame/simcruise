import requests,io,json
from pathlib import Path
from PIL import Image
j=requests.get('https://api.polyhaven.com/files/grass_ground',timeout=8);j.raise_for_status();files=j.json();print(list(files),flush=True)
tex=files['Diffuse']['1k']['jpg']['url'];r=requests.get(tex,timeout=15);r.raise_for_status()
im=Image.open(io.BytesIO(r.content)).convert('RGB');im.thumbnail((640,640));im.save('game/data/grass-detail.jpg',quality=87)
