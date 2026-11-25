"""Cook the approved Blender driver study to bounded GPU-skinning data.

Blender 4.5: --background --python scripts/cook-driver.py -- --model fpe2
Source geometry/rig provenance: assets/drivers/NOTICE.md. No Blender runtime needed.
"""
import argparse, hashlib, json, math, struct, sys
from pathlib import Path
import bpy
import numpy as np
from mathutils import Matrix, Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from driver_cloth import cloth_maps, shape_sleeves

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] / 'assets/drivers')
parser.add_argument('--model', choices=['ascher-artura', 'fpe2'], required=True)
parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'assets/drivers')
args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
source, model = args.source, args.model
bpy.ops.wm.open_mainfile(filepath=str(source/'driver-source.blend'))
shape_sleeves(bpy.data.objects['Arms - visibility independent'])
right=bpy.data.objects['Right hand - Valve anatomical grip rig']
left=bpy.data.objects['Left hand - mirrored anatomical grip rig']
body=bpy.data.objects['DRIVER RIG - wheel-local grips']
if model=='fpe2':
    right.matrix_world.translation+=Vector((.006,.014,.001))
    left.matrix_world.translation+=Vector((-.006,.014,.001))
# A slightly more forward seated position keeps the later descending grip in
# reach without lengthening the arms or stretching the elbow chain.
body.location.y += .05
for obj in bpy.data.objects:
    if obj.type == 'MESH' and obj.name.startswith(('Arms -', 'Torso -', 'Legs -')):
        obj.location.y += .05
bpy.context.view_layer.update()
center=Vector((0,.46,.365))
neutral={h.name:h.matrix_world.copy() for h in [right,left]}
width = .31 if model == 'fpe2' else .30
conversion = Matrix(((1/width,0,0,0),(0,0,1/width,-center.z/width),
                     (0,-1/width,0,center.y/width),(0,0,0,1)))
mirror = Matrix.Diagonal((-1,1,1,1))
rest = json.loads((source/'grasp-poses.json').read_text())['Rest']['right']
grips = {h.name:{p.name:p.matrix_basis.copy() for p in h.pose.bones} for h in [right,left]}

def point(name):return body.data.bones[name].head_local.copy()

def segment(name,start,end):
    bone=body.data.bones[name]
    r=(bone.tail_local-bone.head_local).rotation_difference(end-start).to_matrix()
    m=(r @ bone.matrix_local.to_3x3()).to_4x4();m.translation=start
    body.pose.bones[name].matrix=m
    bpy.context.view_layer.update()

def elbow(s,t,l1,l2,pole):
    delta=t-s;d=delta.length;axis=delta.normalized()
    a=(l1*l1-l2*l2+d*d)/(2*d);v=Vector(pole);v=(v-axis*v.dot(axis)).normalized()
    return s+axis*a+v*math.sqrt(max(0,l1*l1-a*a))

def smooth(x):
    x=max(0,min(1,x)); return x*x*(3-2*x)

def mount(hand,angle):
    return Matrix.Translation(center) @ Matrix.Rotation(math.radians(angle),4,'Y') @ Matrix.Translation(-center) @ neutral[hand.name]

def blend(a,b,t):
    pa,qa,sa=a.decompose(); pb,qb,sb=b.decompose()
    return Matrix.LocRotScale(pa.lerp(pb,t),qa.slerp(qb,t),sa.lerp(sb,t))

def positive_state(hand,angle):
    local=angle-180*min(1,int(angle//180))
    # Later than the approved first study (45 degrees). Continuous at 180/360.
    release, takeover = 70., 90.
    if hand == right:
        if local<=release: return mount(hand,local),0,True
        if local>=takeover:
            m=mount(hand,local-180)
            m.translation += Matrix.Rotation(math.radians(local-180),3,'Y') @ Vector((0,0,-.115*(1-smooth((local-takeover)/30))))
            return m,0,True
        u=(local-release)/(takeover-release)
        end=mount(hand,takeover-180)
        end.translation += Matrix.Rotation(math.radians(takeover-180),3,'Y') @ Vector((0,0,-.115))
        m=blend(mount(hand,release),end,smooth(u))
        m.translation += Vector((.035,-.085,.045))*math.sin(math.pi*u)
        return m,.8*math.sin(math.pi*u),False
    if local<=takeover:return mount(hand,local),0,True
    u=(local-takeover)/(180-takeover)
    m=blend(mount(hand,takeover),mount(hand,0),smooth(u))
    m.translation += Vector((-.035,-.085,.055))*math.sin(math.pi*u)
    return m,.8*math.sin(math.pi*u),u>=1

def pose(native_angle):
    # Blender +Y rotation maps to native -Z, so native CCW reverses it.
    angle=-native_angle
    diagnostics=[]
    for hand,side,sign in [(right,'L',1),(left,'R',-1)]:
        if angle<0:
            other=left if hand==right else right
            matrix,opening,held=positive_state(other,-angle)
            matrix=mirror @ matrix
        else:matrix,opening,held=positive_state(hand,angle)
        hand.matrix_world=matrix
        for p in hand.pose.bones:
            if p.name.startswith('finger_') and p.name in rest:
                p.matrix_basis=blend(grips[hand.name][p.name],Matrix(rest[p.name]),opening)
        bpy.context.view_layer.update()
        wrist=body.matrix_world.inverted() @ hand.matrix_world @ hand.pose.bones['wrist_r'].head
        sh=point('upperarm01.'+side); oldel=point('lowerarm01.'+side); oldwr=point('wrist.'+side)
        l1=(oldel-sh).length; l2=(oldwr-oldel).length
        handaxis=(hand.matrix_world.to_3x3() @ (hand.data.bones['finger_middle_0_r'].head_local-hand.data.bones['wrist_r'].head_local)).normalized()
        handaxis=(body.matrix_world.inverted().to_3x3() @ handaxis).normalized()
        e=elbow(sh,wrist,l1,l2,(sign*.5,-.1,-1))
        # Keep the anatomical finger pose and palm contact anchor. A bounded
        # root roll relieves wrist flexion without lifting elbows across the
        # wheel or releasing earlier than70 degrees. Re-solve reachable wrists.
        palm_anchor=hand.matrix_world @ hand.pose.bones['finger_middle_0_r'].head
        adjusted=0.
        for _ in range(3):
            forearm=(wrist-e).normalized();flex=handaxis.angle(forearm)
            correction=min(max(0.,flex-math.radians(45)),math.radians(24 if held else 35)-adjusted)
            if correction<1e-5:break
            rotation_axis=body.matrix_world.to_3x3() @ handaxis.cross(forearm).normalized()
            rotation=Matrix.Rotation(correction,4,rotation_axis)
            hand.matrix_world=Matrix.Translation(palm_anchor) @ rotation @ Matrix.Translation(-palm_anchor) @ hand.matrix_world
            adjusted+=correction;bpy.context.view_layer.update()
            wrist=body.matrix_world.inverted() @ hand.matrix_world @ hand.pose.bones['wrist_r'].head
            handaxis=(body.matrix_world.inverted().to_3x3() @ hand.matrix_world.to_3x3() @ (hand.data.bones['finger_middle_0_r'].head_local-hand.data.bones['wrist_r'].head_local)).normalized()
            e=elbow(sh,wrist,l1,l2,(sign*.5,-.1,-1))
        u=sh.lerp(e,(point('upperarm02.'+side)-sh).length/l1)
        lo=e.lerp(wrist,(point('lowerarm02.'+side)-oldel).length/l2)
        for bone,a,b in [('upperarm01',sh,u),('upperarm02',u,e),('lowerarm01',e,lo),('lowerarm02',lo,wrist)]:
            segment(bone+'.'+side,a,b)
        # Distal cloth follows the actual glove cuff axis, with the wrist
        # transition blended by sleeve weights. Keep the authored hand pose.
        handaxis=(hand.matrix_world.to_3x3() @ (hand.data.bones['finger_middle_0_r'].head_local-hand.data.bones['wrist_r'].head_local)).normalized()
        handaxis=(body.matrix_world.inverted().to_3x3() @ handaxis).normalized()
        rotation=(oldwr-oldel).rotation_difference(handaxis).to_matrix()
        wrist_matrix=(rotation @ body.data.bones['wrist.'+side].matrix_local.to_3x3()).to_4x4()
        wrist_matrix.translation=wrist
        body.pose.bones['wrist.'+side].matrix=wrist_matrix
        bpy.context.view_layer.update()
        diagnostics.append({'held':held,'reachExcessMm':max(0,(wrist-sh).length-l1-l2)*1000,'wristFlexDegrees':math.degrees(handaxis.angle(wrist-e)),'palmAnchorRollDegrees':math.degrees(adjusted),'elbowPosition':list(body.matrix_world @ e),'wristPosition':list(body.matrix_world @ wrist)})
    assert any(h['held'] for h in diagnostics)
    assert max(h['reachExcessMm'] for h in diagnostics)<.1, (native_angle, diagnostics)
    assert max(h['wristFlexDegrees'] for h in diagnostics)<45.1, (native_angle, diagnostics)
    bpy.context.view_layer.update()
    return diagnostics

# Store mesh coordinates in each source object's undeformed local frame; each
# palette entry includes its fixed mesh-to-rig bind transform and world placement.
objects=[o for o in bpy.data.objects if o.type=='MESH' and
         (o.name.startswith(('Arms -','Torso -','Legs -')) or 'racing glove - Valve' in o.name)]
assert len(objects)==5
vertices=[]; indices=[]; joints=[]; parts=[]
pose(0)
for obj in objects:
    part=0 if 'racing glove' in obj.name else 1 if obj.name.startswith('Arms') else 2 if obj.name.startswith('Torso') else 3
    arm=next(m.object for m in obj.modifiers if m.type=='ARMATURE')
    binding=arm.matrix_world.inverted() @ obj.matrix_world
    # Evaluate subdivision in the undeformed local frame. Blender interpolates
    # deform weights and UVs along with the rest surface.
    modifiers=[(m,m.show_viewport) for m in obj.modifiers if m.type=='ARMATURE']
    for modifier,_ in modifiers: modifier.show_viewport=False
    bpy.context.view_layer.update()
    evaluated=obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh=bpy.data.meshes.new_from_object(evaluated,preserve_all_data_layers=True,depsgraph=bpy.context.evaluated_depsgraph_get())
    for modifier,visible in modifiers: modifier.show_viewport=visible
    bpy.context.view_layer.update()
    mesh.calc_loop_triangles()
    groups={g.index:g.name for g in obj.vertex_groups}
    weights=[]; local_joints={}
    for vertex in mesh.vertices:
        influence=sorted([(groups[g.group],g.weight) for g in vertex.groups if groups[g.group] in arm.data.bones and g.weight>1e-7],key=lambda p:-p[1])[:4]
        assert influence, (obj.name,vertex.index)
        total=sum(w for _,w in influence)
        for name,_ in influence:
            if name not in local_joints:
                local_joints[name]=len(joints); joints.append((arm,name,binding.copy()))
        joint=[local_joints[name] for name,_ in influence];weight=[w/total for _,w in influence]
        weights.append((joint+[0]*(4-len(joint)),weight+[0.]*(4-len(weight))))
    uv=mesh.uv_layers.active
    cache={};start=len(indices)
    for tri in mesh.loop_triangles:
        for li in tri.loops:
            vi=mesh.loops[li].vertex_index;v=mesh.vertices[vi]
            raw=tuple(uv.data[li].uv) if uv else (0.,0.)
            # Separate glove/cloth halves, with an inset to avoid cross-material
            # filtering at their shared edge. No extra runtime texture sample.
            tex=((.5 if part else 0)+(.002+.996*max(0,min(1,raw[0])))*.5,
                 .002+.996*max(0,min(1,raw[1])))
            key=(vi,tex)
            if key not in cache:
                cache[key]=len(vertices);joint,weight=weights[vi]
                vertices.append((*v.co,*v.normal,*tex,float(part),*joint,*weight))
            indices.append(cache[key])
    parts.append({'name':obj.name,'part':part,'firstIndex':start,'indexCount':len(indices)-start})
assert len(joints)<=128,len(joints)
assert len(vertices)<200000

# Palettes only need bone transforms. Disable mesh evaluation during the pose
# sweep to avoid recomputing full-body subdivision ten times per angle.
for obj in objects:
    for modifier in obj.modifiers: modifier.show_viewport=False
bpy.context.view_layer.update()

positions=np.array([v[:3]+(1.,) for v in vertices],dtype=np.float32)
part_ids=np.array([int(v[8]) for v in vertices])
joint_ids=np.array([v[9:13] for v in vertices],dtype=np.uint32)
weights=np.array([v[13:17] for v in vertices],dtype=np.float32)
palettes=[];bounds=[];checks=[]
for angle in range(-360,361,3):
    checks.append({'angle':angle,'hands':pose(angle)})
    matrices=np.array([list(conversion @ arm.matrix_world @ arm.pose.bones[name].matrix @ arm.data.bones[name].matrix_local.inverted() @ binding)[:3]
                       for arm,name,binding in joints],dtype=np.float32)
    assert np.isfinite(matrices).all()
    deformed=sum(np.einsum('nij,nj->ni',matrices[joint_ids[:,i]],positions)*weights[:,i,None] for i in range(4))
    palettes.append(matrices)
    bounds.append([[*deformed[part_ids==part].min(axis=0),*deformed[part_ids==part].max(axis=0)] for part in range(4)])
    if angle%90==0:print('COOK_POSE',model,angle,flush=True)

# Every half-turn ends on the same neutral arm/hand pose, without a wrap jump.
for index in [0,60,180,240]:assert np.allclose(palettes[index],palettes[120],atol=1e-4)
max_elbow_step=max((Vector(a['elbowPosition'])-Vector(b['elbowPosition'])).length*1000
    for f,g in zip(checks,checks[1:]) for a,b in zip(f['hands'],g['hands']))
assert max_elbow_step<100.,max_elbow_step
image=bpy.data.images.get('vr_glove_color_red.jpg')
assert image and image.size[:]==(1024,1024)
rgba=np.empty(1024*1024*4,dtype=np.float32);image.pixels.foreach_get(rgba)
# Blender loads byte color images as their stored sRGB values; GPU SRV performs decode.
glove_color=np.clip(np.round(rgba*255),0,255).astype(np.uint8).reshape(1024,1024,4)
cloth_color,cloth_normal=cloth_maps()
pixels=np.concatenate((glove_color,cloth_color),axis=1).tobytes()
data=bytearray(struct.pack('<8s7I',b'AGDRVR1\0',4,len(vertices),len(indices),len(joints),len(palettes),2048,1024))
for v in vertices:data.extend(struct.pack('<9f4I4f',*v))
data.extend(np.asarray(indices,dtype='<u4').tobytes())
data.extend(np.asarray(palettes,dtype='<f4').tobytes())
data.extend(np.asarray(bounds,dtype='<f4').tobytes())
data.extend(pixels)
normal=bpy.data.images.get('vr_glove_normal.png')
assert normal and normal.size[:]==(1024,1024)
normal_pixels=np.empty(1024*1024*4,dtype=np.float32);normal.pixels.foreach_get(normal_pixels)
glove_normal=np.clip(np.round(normal_pixels*255),0,255).astype(np.uint8).reshape(1024,1024,4)
data.extend(np.concatenate((glove_normal,cloth_normal),axis=1).tobytes())
args.output.mkdir(parents=True,exist_ok=True)
name=model+'.agd';(args.output/name).write_bytes(data)
manifest={'file':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
          'format':4,'vertices':len(vertices),'triangles':len(indices)//3,'joints':len(joints),'frames':len(palettes),
          'releaseAngleDegrees':70,'takeoverAngleDegrees':90,'parts':parts,
          'maxReachExcessMm':max(h['reachExcessMm'] for f in checks for h in f['hands']),
          'maxWristFlexDegrees':max(h['wristFlexDegrees'] for f in checks for h in f['hands']),
          'maxNeighbourElbowStepMm':max_elbow_step,
          'palmAnchorRootRollLimitDegrees':24,'freeHandRootRollLimitDegrees':35}
(args.output/(model+'.driver.json')).write_text(json.dumps(manifest,indent=2)+'\n')
(args.output/(model+'.checks.json')).write_text(json.dumps(checks))
print('COOK_DONE',json.dumps(manifest),flush=True)
