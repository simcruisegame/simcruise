import glob, os, numpy as np
from PIL import Image
os.makedirs('unbaked',exist_ok=True)
for f in sorted(glob.glob('a-*.jpg')):
    _,ax,ay=f[:-4].split('-'); ax,ay=int(ax),int(ay)
    img=np.asarray(Image.open(f).convert('RGB')).copy(); q=img.shape[0]//4; n=0
    for qx in range(4):
        for qy in range(4):
            p=f'out-{ax*4+qx}-{ay*4+qy}.npy'
            if os.path.exists(p): img[qy*q:(qy+1)*q,qx*q:(qx+1)*q]=np.load(p); n+=1
    Image.fromarray(img).save('unbaked/'+f,quality=92); print(f,n,'tiles unbaked')
