"""One-time authoring repair for the original imported driver source.
The shipped driver-source.blend already contains this repair.
"""
import bpy,bmesh,math
from pathlib import Path
from mathutils import Vector
path=Path('assets/drivers/driver-source.blend').resolve();bpy.ops.wm.open_mainfile(filepath=str(path))
body=bpy.data.objects['DRIVER RIG - wheel-local grips'];ob=bpy.data.objects['Arms - visibility independent']
if ob.get('anatomy_repair'):raise RuntimeError('Sleeves already repaired; use the original source for this one-time operation.')
# Retopologize the split sleeves as continuous anatomical sections. The
# garment's threshold-based torso split left sawtooth chest flaps crossing the
# entire upper arm; closing those edges alone keeps the incorrect silhouette.
# Sample radial cloth variation from the original sleeve, then bound it to a
# tailored sleeve envelope. Shoulder caps remain underneath the optional torso.
from mathutils.bvhtree import BVHTree
original=ob.data.copy();tree=BVHTree.FromPolygons([v.co for v in original.vertices],[list(p.vertices) for p in original.polygons])
verts=[];faces=[];uvs=[];rings=41;around=32
for side,sign in [('L',1),('R',-1)]:
 sh=body.data.bones['upperarm01.'+side].head_local;e=body.data.bones['lowerarm01.'+side].head_local;w=body.data.bones['wrist.'+side].head_local
 a=e-sh;b=w-e;la=a.length;lb=b.length;a.normalize();b.normalize();base=len(verts)
 for j in range(rings):
  t=j/(rings-1);distance=t*(la+lb-.008)-.012
  c=sh+a*distance if distance<la else e+b*(distance-la)
  transition=max(0,min(1,(distance-la+.045)/.09));axis=a.lerp(b,transition).normalized()
  u=Vector((0,1,0));u=(u-axis*u.dot(axis)).normalized();v=axis.cross(u)
  nominal=(.054*(1-t)+.026*t)
  if j==0:nominal=.041
  for k in range(around):
   phi=2*math.pi*k/around;radial=u*math.cos(phi)+v*math.sin(phi)
   hit=tree.ray_cast(c,radial,.10)[0]
   sampled=(hit-c).length if hit is not None else nominal
   variation=max(-.004,min(.004,sampled-nominal))
   # Keep original garment variation but suppress the disconnected torso flaps.
   fold_envelope=math.exp(-((distance-la)/.052)**2)+.55*math.exp(-((distance-la-lb+.065)/.04)**2)
   fold=.0015*fold_envelope*math.sin(distance*380+2*phi)
   radius=nominal+variation*.65+fold+.005*math.exp(-((distance-la)/.045)**2)
   verts.append(tuple(c+radial*radius));uvs.append((k/around,t))
 for j in range(rings-1):
  for k in range(around):
   a0=base+j*around+k;a1=base+j*around+(k+1)%around
   faces.append((a0,a1,a1+around,a0+around))
 # Shoulder cap is a real closed mesh, not a view-dependent hiding plane.
 faces.append(tuple(base+k for k in reversed(range(around))))
mesh=bpy.data.meshes.new('Anatomical continuous sleeve retopology');mesh.from_pydata(verts,[],faces);mesh.update()
for m in original.materials:mesh.materials.append(m)
ob.data=mesh
uv=mesh.uv_layers.new(name='Sleeve woven panels')
for p in mesh.polygons:
 p.use_smooth=True
 seam=any(vi%around==around-1 for vi in p.vertices) and any(vi%around==0 for vi in p.vertices)
 for li,vi in zip(p.loop_indices,p.vertices):
  u,t=uvs[vi];uv.data[li].uv=(1. if seam and u==0 else u,t)
# The ray-sampled rings wind outward along the anatomical axis.
# A continuous sleeve uses only the four arm segments. The old garment map
# blended isolated underarm vertices with static chest and finger bones.
for g in ob.vertex_groups:g.remove(list(range(len(ob.data.vertices))))
for side in ['L','R']:
 for stem in ['upperarm01','upperarm02','lowerarm01','lowerarm02','wrist']:
  if stem+'.'+side not in ob.vertex_groups:ob.vertex_groups.new(name=stem+'.'+side)
for v in ob.data.vertices:
 side='L' if v.co.x>0 else 'R';sh=body.data.bones['upperarm01.'+side].head_local;e=body.data.bones['lowerarm01.'+side].head_local;w=body.data.bones['wrist.'+side].head_local
 a=(e-sh);b=(w-e);la=a.length;lb=b.length;a.normalize();b.normalize()
 tu=max(0,min(la,(v.co-sh).dot(a)));tl=max(0,min(lb,(v.co-e).dot(b)))
 du=(v.co-(sh+a*tu)).length;dl=(v.co-(e+b*tl)).length
 t=((v.index%(rings*around))//around)/(rings-1)*(la+lb-.008)-.012
 # Smooth distance weights around the elbow, preserving cloth volume.
 elbow_weight=max(0,min(1,(t-la+.060)/.12));elbow_weight=elbow_weight*elbow_weight*(3-2*elbow_weight)
 upper2=max(0,min(1,(t-.04)/.06));lower2=max(0,min(1,(t-la-.06)/.10))
 ws={'upperarm01':(1-elbow_weight)*(1-upper2),'upperarm02':(1-elbow_weight)*upper2,'lowerarm01':elbow_weight*(1-lower2),'lowerarm02':elbow_weight*lower2}
 wrist_weight=max(0,min(1,(t-la-lb+.11)/.075));wrist_weight=wrist_weight*wrist_weight*(3-2*wrist_weight)
 ws={name:value*(1-wrist_weight) for name,value in ws.items()};ws['wrist']=wrist_weight
 for name,value in ws.items():
  if value>0:ob.vertex_groups[name+'.'+side].add([v.index],value,'REPLACE')
# Use the same smooth rest surface followed by skinning in Blender and native.
for o in bpy.data.objects:
 if o.type=='MESH':
  sub=next((m for m in o.modifiers if m.type=='SUBSURF'),None)
  if sub and o.name.startswith(('Torso','Legs')):sub.levels=0;sub.render_levels=0
  if sub:
   bpy.context.view_layer.objects.active=o
   bpy.ops.object.modifier_move_up(modifier=sub.name)
ob['anatomy_repair']='Continuous retopologized sleeves, sampled cloth variation, arm-only weights, closed shoulders and tapered cuffs; rest subdivision precedes skinning.'
bpy.ops.wm.save_as_mainfile(filepath=str(path))
