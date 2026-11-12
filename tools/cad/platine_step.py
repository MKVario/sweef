# STEP-Modell der bestückten Sweef-Platine v5 (für die Gehäusekonstruktion in Fusion)
# Koordinaten: Blick auf die Vorderseite (Display), x nach rechts, y nach oben (Lasche unten, y=0),
# z=0 Rückseite der Platine, Platine z=0..1,6, Vorderseite (+z) = Display.
import cadquery as cq, xml.etree.ElementTree as ET, sys
BRD=sys.argv[1]; OUT=sys.argv[2]
r=ET.parse(BRD).getroot()
T=1.6
# Umriss
outline=(cq.Workplane("XY").moveTo(5.6,0).lineTo(28.6,0).radiusArc((29.6,1),-1).lineTo(29.6,8).lineTo(33.2,8)
  .radiusArc((34.2,9),-1).lineTo(34.2,45.2).radiusArc((33.2,46.2),-1).lineTo(1,46.2).radiusArc((0,45.2),-1)
  .lineTo(0,9).radiusArc((1,8),-1).lineTo(4.6,8).lineTo(4.6,1).radiusArc((5.6,0),-1).close().extrude(T))
# Loecher BAT+/BAT-
outline=outline.faces(">Z").workplane().pushPoints([(8.55,22.2),(10.48,22.2)]).hole(0.9)
asm=cq.Assembly(name="Sweef_Platine_v5")
asm.add(outline,name="Platine_1,6mm",color=cq.Color(0.12,0.42,0.27))
def box(name,x0,x1,y0,y1,z0,z1,col):
    asm.add(cq.Workplane("XY").box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)),name=name,color=cq.Color(*col))
# Bauteile Rueckseite aus der Platinendatei
H={"R0402":(1.0,0.5,0.35),"C0402":(1.0,0.5,0.5),"0603-NO":(1.6,0.8,0.8),"0805-NO":(2.0,1.25,1.25),
   "MSOP10":(3.0,4.9,1.1),"MSOP8_0.65MM":(3.0,4.9,1.1),"SOT23-5":(2.9,2.8,1.3),"INDUCTOR_3X3":(3.0,3.0,2.0),
   "SOD-123":(3.7,1.6,1.25),"BMX280_COMBO":(2.0,2.5,1.0),"LGA12_2X2MM":(2.0,2.0,0.75),
   "FPC-24_XUNPU_FPC-05FB-24PH20":(16.6,4.7,2.0)}
schwarz=(0.08,0.08,0.08); beige=(0.75,0.68,0.55)
for e in r.iter('element'):
    n,p=e.get('name'),e.get('package')
    if p not in H: continue
    w,h,hz=H[p]; rot=e.get('rot') or "R0"
    if rot.endswith("90") or rot.endswith("270"): w,h=h,w
    x,y=float(e.get('x')),float(e.get('y'))
    vorne = n in ("C20","C21","C22","R20","R21")
    z0,z1=(T,T+hz) if vorne else (-hz,0)
    col = beige if p.startswith(("R0","C0","06","08")) else schwarz
    if p.startswith("FPC"): col=(0.9,0.9,0.88)
    box(f"{n}_{p}",x-w/2,x+w/2,y-h/2,y+h/2,z0,z1,col)
# XIAO nRF52840 Sense (Rueckseite) – Maße ungefähr, bitte nachmessen
XY0=0.29; YC=17.72
box("XIAO_Platine",XY0,XY0+21.0,YC-8.9,YC+8.9,-1.0,0,(0.15,0.15,0.5))
box("XIAO_Bauteile",XY0+7.0,XY0+20.0,YC-7.0,YC+7.0,-2.6,-1.0,(0.6,0.6,0.62))
box("XIAO_USB-C",-1.0,6.35,YC-4.47,YC+4.47,-4.2,-1.0,(0.75,0.75,0.78))
# Displayglas GME128128-02 33,9 x 37,3 x 1,45 (vorne, 0,2 mm Klebeband)
gx0=17.1-33.9/2; gy0=27.1-37.3/2
box("Displayglas_GME128128-02",gx0,gx0+33.9,gy0,gy0+37.3,T+0.2,T+1.65,(0.05,0.05,0.07))
box("Display_aktive_Flaeche",17.1-13.43,17.1+13.43,27.1+37.3/2-1.9-26.86,27.1+37.3/2-1.9,T+1.65,T+1.66,(0.1,0.3,0.6))
# Flachbandkabel 12,5 breit um die Laschenkante (Schlaufe ca. 1,5 mm über die Kante)
fx0=17.1-6.25; fx1=17.1+6.25; gelb=(0.85,0.6,0.15)
box("FPC_vorne",fx0,fx1,-1.5,gy0,T+1.0,T+1.3,gelb)
box("FPC_Schlaufe",fx0,fx1,-1.8,-1.5,-1.3,T+1.3,gelb)
box("FPC_hinten",fx0,fx1,-1.8,1.5,-1.3,-1.0,gelb)
# Akku 503035 (5 x 30 x 35) hinter dem XIAO – Lage frei wählbar
box("Akku_503035_Lage_frei",17.1-15,17.1+15,27.1-17.5,27.1+17.5,-4.7-5.0,-4.7,(0.55,0.55,0.6))
# Lautsprecher LSF-S1318A 18 x 13 x 2,5 – Lage frei wählbar (Kammer oben)
box("Lautsprecher_LSF-S1318A_Lage_frei",17.1-9,17.1+9,47.5,60.5,0,2.5,(0.2,0.2,0.2))
asm.save(OUT)
bb=asm.toCompound().BoundingBox(); print("Gesamt", round(bb.xlen,1), round(bb.ylen,1), round(bb.zlen,1), "z", round(bb.zmin,2), round(bb.zmax,2))
