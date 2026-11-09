import numpy as np,struct,subprocess,sys
from pathlib import Path
D=Path(__file__).parent
room=Path(sys.argv[1]);f=room/'needles-volume.meshdata';raw=f.read_bytes();h=struct.unpack_from('<IfffII',raw);count,nt=h[-2:];start=24;arrays=[]
for n,d in [(3,'<f4'),(3,'<f4'),(2,'<f4'),(2,'<f4'),(4,'u1')]:
 a=np.frombuffer(raw,d,count=count*n,offset=start).reshape(count,n);start+=a.nbytes;arrays.append(a)
tri=np.frombuffer(raw,'<u4',offset=start);tmp=room/'input.bin';tmp.write_bytes(struct.pack('<2I',count,len(tri))+arrays[0].tobytes()+tri.tobytes());out=room/'tri.bin';from scipy.spatial import cKDTree
locks=room/'lock.bin';locked=np.zeros(count,'u1');tree=cKDTree(arrays[0])
binary=room/'meshopt-authoring'
subprocess.run(['c++','-std=c++17','-O2','-o',str(binary),
                *[str(D/'meshopt'/n) for n in ('simplify.cpp','simplifier.cpp','allocator.cpp')]],check=True)
for stage in range(10):
 locks.write_bytes(locked.tobytes())
 subprocess.run([str(binary.resolve()),str(tmp),str(out),str(locks)],check=True)
 tr=np.fromfile(out,'<u4');ids,reverse=np.unique(tr,return_inverse=True);triangle=reverse.reshape(-1,3)
 eg=np.r_[triangle[:,[0,1]],triangle[:,[1,2]],triangle[:,[2,0]]];edges,n=np.unique(np.sort(eg,axis=1),axis=0,return_counts=True);irregular=edges[n!=2]
 if len(irregular)==0:break
 rr=tree.query_ball_point(arrays[0][ids[irregular.ravel()]],1.+.15*stage)
 for group in rr:locked[group]=1
else:raise ValueError('closed mesh decimation check failed')
result=struct.pack('<IfffII',*h[:4],len(ids),len(tr))+b''.join(a[ids].tobytes() for a in arrays)+reverse.astype('<u4').tobytes()
# Store the flight's absolute reference alongside the reduced vertices. The
# matched ground patch supplies the projection's measured vertical offset.
zref=float(np.load(D/'survey.npz')['zref'])
result=struct.pack('<I',4)+result[4:]+struct.pack('<f',zref)
print('vertices',len(ids),'triangles',len(tr)//3,'non-manifold/boundary edges',len(irregular),'file bytes',len(result));f.write_bytes(result);tmp.unlink();out.unlink();locks.unlink();binary.unlink()
