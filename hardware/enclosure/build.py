"""Export matching manifold CAD meshes and a dimensionally derived preview.
Requires numpy, manifold3d, trimesh, Pillow. No OpenSCAD executable needed.
"""
from pathlib import Path
import math, json
import numpy as np
import manifold3d as m
import trimesh
from PIL import Image, ImageDraw, ImageFont
OUT=Path(__file__).resolve().parent
M=m.Manifold

def box(w,h,d,x=0,y=0,z=0):return M.cube([w,h,d]).translate([x-w/2,y-h/2,z])
def cyl(x,y,z,d,h):return M.cylinder(h,d/2,circular_segments=48).translate([x,y,z])
def add(items):
    r=M()
    for v in items:r=r+v
    return r
body=box(232,68,45.6)-box(227.2,63.2,48,z=2.4)
body=body+add(cyl(x,y,2.4,8,43.2) for x in [-111,111] for y in [-29,29])
body=body-box(168,32,5,-4,-4,-1)
for x in [-111,111]:
    for y in [-29,29]:body=body-cyl(x,y,31,2.6,18)
for x in range(-78,79,12):body=body-box(7,6,2,x,33,35)
body=body-box(12,8,9,0,-32,40)
lid=box(232,68,2.4,z=45.6)+add(cyl(x,y,18.9,5.5,26.8) for x in [-103.5,103.5] for y in [-21,21])
for x in [-103.5,103.5]:
    for y in [-21,21]:lid=lid-cyl(x,y,18.8,2.6,13)
for x in [-111,111]:
    for y in [-29,29]:lid=lid-cyl(x,y,44.6,3.4,4.4)
for x in range(-75,76,10):
    for y in [-22,22]:lid=lid-box(5,2,5,x,y,44.6)
s,c=math.sin(math.radians(15)),math.cos(math.radians(15))
def pose(v,explode=0):return v.translate([0,0,explode]).transform([[1,0,0,0],[0,s,c,0],[0,c,-s,49.3]])
# Wedge profile is in world Y/Z, extruded across X.
wedge=m.CrossSection([np.array([[58,4],[-12,19],[-12,4]],float)]).extrude(12)
wedge=wedge.transform([[0,0,1,-6],[1,0,0,0],[0,1,0,0]])
stand=box(232,74,4,0,23)+wedge.translate([-82,0,0])+wedge.translate([82,0,0])
stand=stand-pose(box(233,168,48.6,0,50,-0.3))
for x in [-101,101]:
    for y in [-4,50]:stand=stand-cyl(x,y,-0.1,10,0.9)
def mesh(v):
    a=v.to_mesh();return trimesh.Trimesh(np.asarray(a.vert_properties)[:,:3],np.asarray(a.tri_verts),process=True)
report={}
for name,v in [('body',body),('lid',lid.transform([[1,0,0,0],[0,-1,0,0],[0,0,-1,48]])),('stand',stand)]:
    t=mesh(v); print(name, v.status(), t.is_watertight, t.volume, flush=True); assert t.is_watertight and t.is_winding_consistent and t.volume>0
    assert len(v.decompose())==1,(name,'disconnected components')
    t.export(OUT/(name+'.stl'))
    report[name]={'watertight':bool(t.is_watertight),'triangles':len(t.faces),'bounds_mm':t.extents.round(3).tolist(),'volume_cm3':round(t.volume/1000,2)}
(OUT/'validation.json').write_text(json.dumps(report,indent=2))
# Orthographic CAD preview, generated from the same solid geometry.
img=Image.new('RGB',(1600,1050),'#eef1f4');draw=ImageDraw.Draw(img)
fontpath='/System/Library/Fonts/Supplemental/Arial.ttf'
font=ImageFont.truetype(fontpath,24);small=ImageFont.truetype(fontpath,19);title=ImageFont.truetype(fontpath,38)
draw.text((65,40),'VFD / DESK 15',font=title,fill='#172b37')
draw.text((65,95),'M202MD28AA  |  232 mm wide  |  15 deg backward tilt',font=font,fill='#526675')
def render(items,center,scale):
    global draw
    eye=np.array([0.75,-1.6,0.95]);eye/=np.linalg.norm(eye)
    right=np.cross([0,0,1],eye);right/=np.linalg.norm(right);up=np.cross(eye,right)
    faces=[]
    for solid,col in items:
        t=mesh(solid);v=t.vertices; p=np.c_[v@right,v@up]*scale;p[:,1]*=-1;p+=center
        for f,n in zip(t.faces,t.face_normals):
            if n@eye<=0:continue
            light=.63+.37*max(0,n@np.array([-.3,-.5,.81]))
            color=tuple(int(min(255,k*light)) for k in col)
            faces.append((v[f]@eye,p[f],color))
    pixels=np.array(img); depth=np.full((img.height,img.width),-np.inf)
    for zz,p,col in faces:
        lo=np.maximum(np.floor(p.min(axis=0)).astype(int),[0,0]);hi=np.minimum(np.ceil(p.max(axis=0)).astype(int),[img.width-1,img.height-1])
        if np.any(hi<lo):continue
        xx,yy=np.meshgrid(np.arange(lo[0],hi[0]+1)+.5,np.arange(lo[1],hi[1]+1)+.5)
        a,b,c=p;den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(den)<1e-9:continue
        u=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/den
        vv=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/den
        w=1-u-vv;z=u*zz[0]+vv*zz[1]+w*zz[2]
        region=depth[lo[1]:hi[1]+1,lo[0]:hi[0]+1];mask=(u>=0)&(vv>=0)&(w>=0)&(z>region)
        region[mask]=z[mask];pixels[lo[1]:hi[1]+1,lo[0]:hi[0]+1][mask]=col
    img.paste(Image.fromarray(pixels));draw=ImageDraw.Draw(img)
pcb=box(215,50,1.6,z=17.3)
glass=box(195.2,40,12.2,-7.5,0,5.1)
active=box(160.5,23.9,.2,-4,-4,4.9)
# Individual glyphs set in the actual 20x2 active region, viewed through opening.
glyph=[]
for row,text in enumerate(['2026-11 14:32:10 FRI','IN:24.6C RH:51.2%']):
    for i,ch in enumerate(text):
        mask=Image.new('1',(6,9));ImageDraw.Draw(mask).text((0,-2),ch,font=ImageFont.load_default(),fill=1)
        for py in range(9):
            for px in range(6):
                if mask.getpixel((px,py)):glyph.append(box(.65,.65,.15,-80+8.1*i+px*.9,4.5-row*13.33-py*.9,4.6))
textsolid=add(glyph)
items=[(stand,(55,66,76)),(pose(body),(73,84,94)),(pose(lid),(92,102,112)),(pose(pcb),(32,85,60)),(pose(glass),(14,25,28)),(pose(active),(16,42,43)),(pose(textsolid),(90,255,194))]
render(items,(490,515),3.4)
draw.text((65,650),'ASSEMBLED / rear cable exit / removable cradle',font=small,fill='#526675')
render([(pose(body),(73,84,94)),(pose(lid,52),(126,143,155)),(pose(pcb,18),(32,100,68)),(pose(glass,18),(15,40,40)),(stand,(55,66,76))],(1210,530),2.1)
draw.text((1000,650),'EXPLODED / four PCB standoffs',font=small,fill='#526675')
# Side view from exact front/rear corner transform.
x0,y0=100,925; k=2.4
poly=[]
for y,z in [(-34,0),(34,0),(34,48),(-34,48)]:poly.append((x0+(s*y+c*z+14)*k,y0-(c*y-s*z+49.3)*k))
draw.polygon(poly,fill='#526675');draw.rectangle([x0,y0-4*k,x0+74*k,y0],fill='#263945')
draw.text((330,780),'SIDE PROFILE',font=font,fill='#172b37')
draw.text((330,825),'15 deg from vertical / 75 deg from desktop',font=small,fill='#526675')
draw.text((330,860),'Overall: approx. 232 W x 74 D x 83 H mm',font=small,fill='#526675')
draw.text((330,895),'Front opening: 168 x 32 mm; PCB pitch: 207 x 42 mm',font=small,fill='#526675')
draw.text((65,995),'PROTOTYPE V1  |  Connector, glass offset and sensor placement require physical fit check.',font=small,fill='#526675')
draw=ImageDraw.Draw(img)
draw.text((65,40),'VFD / DESK 15',font=title,fill='#172b37')
draw.text((65,95),'M202MD28AA  |  232 mm wide  |  15 deg backward tilt',font=font,fill='#526675')
draw.text((65,650),'ASSEMBLED / rear cable exit / removable cradle',font=small,fill='#526675')
draw.text((1000,650),'EXPLODED / four PCB standoffs',font=small,fill='#526675')
draw.polygon(poly,fill='#526675');draw.rectangle([x0,y0-4*k,x0+74*k,y0],fill='#263945')
draw.text((330,780),'SIDE PROFILE',font=font,fill='#172b37')
draw.text((330,825),'15 deg from vertical / 75 deg from desktop',font=small,fill='#526675')
draw.text((330,860),'Overall: approx. 232 W x 74 D x 83 H mm',font=small,fill='#526675')
draw.text((330,895),'Front opening: 168 x 32 mm; PCB pitch: 207 x 42 mm',font=small,fill='#526675')
draw.text((65,995),'PROTOTYPE V1  |  Connector, glass offset and sensor placement require physical fit check.',font=small,fill='#526675')
img.save(OUT/'preview.png')
print(json.dumps(report,indent=2))
