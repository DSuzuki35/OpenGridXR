"""Original, deterministic racing-fabric artwork and sleeve shaping.

Runs only while cooking assets. No cloth simulation or procedural fragment cost
is added to the game. RGB is neutral sRGB fabric; alpha is untinted stitching.
"""
import math
import numpy as np


def cloth_maps(size=1024):
    v, u = np.mgrid[0:size, 0:size].astype(np.float32) / size
    tau = 2 * math.pi
    # Nonuniform diagonal compression wrinkles, concentrated near elbow/cuff.
    envelope = np.exp(-((v-.52)/.14)**2) + .65*np.exp(-((v-.88)/.08)**2)
    phase = tau*(13*v + .24*np.sin(tau*2*u) + .12*np.sin(tau*3*v))
    folds = .0008*envelope*(np.sin(phase)+.28*np.sin(phase*1.7+u*tau))
    # Twill weave is baked, then minified by the normal/diffuse mip chain.
    weave = np.sin(tau*(u*220+v*150))*np.sin(tau*(u*220-v*150))
    height = folds + .000035*weave
    shade = .62 + .045*np.sin(phase)*envelope + .035*weave
    for center,width,amplitude in [(.45,.019,.0012),(.53,.014,.0015),(.61,.021,.0008),(.80,.014,.0011),(.86,.010,.0013)]:
        curve=center+.022*np.sin(tau*u+center*7)+.008*np.sin(tau*3*u)
        ridge=np.exp(-((v-curve)/width)**2)
        valley=np.exp(-((v-curve-width*1.4)/(width*.7))**2)
        height += amplitude*(ridge-.35*valley)
        shade *= 1-.10*valley
    panel = np.maximum(np.clip((u-.18)*130,0,1)*np.clip((.35-u)*130,0,1),
                       np.clip((u-.65)*130,0,1)*np.clip((.82-u)*130,0,1))
    shade *= 1-.52*panel
    stitching = np.zeros_like(u)
    for seam in [.04,.18,.35,.65,.82,.96]:
        distance = np.abs(u-seam-.003*np.sin(tau*v*2))
        ridge = np.exp(-(distance/.0022)**2)
        groove = np.exp(-((distance-.005)/.0012)**2)
        height += .00038*ridge-.00012*groove
        shade *= 1-.20*groove
        # Two fine rows of separate thread stitches beside each seam.
        stitch = np.exp(-((distance-.008)/.00085)**2)
        stitch *= np.clip(np.sin(tau*v*100)*4+.5,0,1)
        stitching = np.maximum(stitching,stitch*.65)
    # A narrow stitched cuff transition is visible at the glove connection.
    for cuff in [.90,.917]:
        distance=np.abs(v-cuff)
        height += .00020*np.exp(-(distance/.002)**2)
        stitch=np.exp(-(distance/.001)**2)*np.clip(np.sin(tau*u*120)*4+.5,0,1)
        stitching=np.maximum(stitching,stitch*.55)
    shade = shade*(1-stitching)+.70*stitching
    color = np.empty((size,size,4),np.uint8)
    color[:,:,:3] = np.clip(shade[:,:,None]*255,0,255).astype(np.uint8)
    color[:,:,3] = np.clip(stitching*255,0,255).astype(np.uint8)
    # Metric gradients: circumference about 22cm, sleeve length about 60cm.
    du = (np.roll(height,-1,axis=1)-np.roll(height,1,axis=1))/(.44/size)
    dv = np.gradient(height,axis=0)/(.60/size)
    n = np.stack((-du,-dv,np.ones_like(u)),axis=2)
    n /= np.linalg.norm(n,axis=2)[:,:,None]
    normal = np.empty_like(color)
    normal[:,:,:3] = np.clip((n*.5+.5)*255+.5,0,255).astype(np.uint8)
    normal[:,:,3] = 255
    return color,normal


def shape_sleeves(obj):
    """Add modest cloth volume without moving wrist/shoulder endpoints or weights."""
    rings,around = 41,32
    assert len(obj.data.vertices)==2*rings*around
    for side in range(2):
        start=side*rings*around
        for j in range(rings):
            t=j/(rings-1)
            ring=[obj.data.vertices[start+j*around+k] for k in range(around)]
            center=sum((v.co for v in ring),ring[0].co*0)/around
            envelope=math.exp(-((t-.52)/.16)**2)+.6*math.exp(-((t-.87)/.09)**2)
            endfade=min(1,j/4,(rings-1-j)/4)
            for k,vertex in enumerate(ring):
                u=k/around; radial=(vertex.co-center).normalized()
                phase=2*math.pi*(8*t+.20*math.sin(2*math.pi*u))
                fold=.0042*envelope*(math.sin(phase)+.25*math.sin(phase*1.8))
                ease=.0035*math.sin(math.pi*t)**2
                vertex.co += radial*(fold+ease)*endfade
    obj.data.update()
