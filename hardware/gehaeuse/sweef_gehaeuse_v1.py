# Sweef Vario - Gehaeuse v1 fuer Platine v4 (34,2 x 51,5 mm)
# Fusion 360 Skript: baut Gehaeuse, Deckel, 2 Taster-Stoessel und Referenzteile
# (Platine, Displayglas, Akku, XIAO, Lautsprecher) in einem neuen Dokument.
#
# Koordinaten (mm): X = Breite, Y = Laenge (USB unten bei Y<0), Z = Tiefe
#   Z = 0 ist die Vorderseite (Display), Z waechst nach hinten zum Deckel.

import adsk.core, adsk.fusion, traceback, math

# ===================== PARAMETER (mm) =====================
PL_B, PL_L, PL_D = 34.2, 51.5, 1.6      # Platine
SPALT = 0.3                             # Luft Platine <-> Innenwand (seitlich)
UNTEN_FREI = 1.8                        # Platz unten (USB ragt 1,5 mm ueber Platine)
OBEN_FREI = 1.8                         # Platz oben (Flexkabel laeuft um die Kante)
WAND = 2.0                              # Seitenwaende
FRONT = 2.0                             # Frontwand
DECKEL = 2.0                            # Deckelstaerke
GLAS_D = 1.5                            # Dicke Displayglas (bitte nachmessen!)
KLEBER = 0.2                            # Klebeband Glas <-> Platine
HINTEN_FREI = 8.5                       # Platz hinter der Platine (Schaum 1,5 + Akku 6 + Luft)
R_AUSSEN, R_INNEN = 3.0, 1.0            # Eckradien

# abgeleitet
Z_PV = FRONT + GLAS_D + KLEBER          # Platine Vorderseite
Z_PH = Z_PV + PL_D                      # Platine Rueckseite
Z_DI = Z_PH + HINTEN_FREI               # Deckel Innenseite = Oberkante Gehaeuse
Z_DA = Z_DI + DECKEL                    # Deckel Aussenseite
IX0, IX1 = -SPALT, PL_B + SPALT
IY0, IY1 = -UNTEN_FREI, PL_L + OBEN_FREI
AX0, AX1 = IX0 - WAND, IX1 + WAND
AY0, AY1 = IY0 - WAND, IY1 + WAND

# Positionen auf der Platine
LOCH_UNTEN = [(2.8, 2.8), (PL_B - 2.8, 2.8)]       # Schrauben von vorne (M2 x 10)
LOCH_OBEN = [(2.2, PL_L - 1.3), (PL_B - 2.0, PL_L - 1.3)]   # Stifte vom Deckel
TASTER = [(6.0, 8.0), (28.2, 8.0)]
TASTER_H = 1.5
LS = (17.1, 7.3); LS_D = 13.0
FENSTER = (3.25, 16.5, 30.95, 44.2)     # sichtbare Displayflaeche + 0,4 mm Rand
GLAS = (0.15, PL_L - 37.0, 34.05, PL_L + 0.3)
AKKU = (0.6, 21.8, 33.6, 41.3); AKKU_D = 6.0; SCHAUM = 1.5
USB_X, USB_B, USB_H = 17.1, 8.94, 3.2
XIAO_D = 1.2
SCHRAUBE_OBEN = (17.1, IY1 - 0.5)       # Deckelschraube oben (M2 x 6)

MM = 0.1   # Fusion rechnet intern in cm

def P(x, y):
    return adsk.core.Point3D.create(x * MM, y * MM, 0)

def V(v):
    return adsk.core.ValueInput.createByReal(v * MM)

def skizze(comp):
    return comp.sketches.add(comp.xYConstructionPlane)

def rrect(sk, x0, y0, x1, y1, r=0.0):
    L = sk.sketchCurves.sketchLines
    if r <= 0:
        L.addTwoPointRectangle(P(x0, y0), P(x1, y1))
        return
    A = sk.sketchCurves.sketchArcs
    ecken = [((x1 - r, y0 + r), (x1 - r, y0)),     # rechts unten
             ((x1 - r, y1 - r), (x1, y1 - r)),     # rechts oben
             ((x0 + r, y1 - r), (x0 + r, y1)),     # links oben
             ((x0 + r, y0 + r), (x0, y0 + r))]     # links unten
    boegen = [A.addByCenterStartSweep(P(*m), P(*st), math.pi / 2) for m, st in ecken]
    for i in range(4):
        L.addByTwoPoints(boegen[i].endSketchPoint, boegen[(i + 1) % 4].startSketchPoint)

def kreis(sk, x, y, d):
    sk.sketchCurves.sketchCircles.addByCenterRadius(P(x, y), d / 2 * MM)

def extr(comp, sk, z0, z1, op, body=None):
    profs = adsk.core.ObjectCollection.create()
    for i in range(sk.profiles.count):
        profs.add(sk.profiles.item(i))
    ex = comp.features.extrudeFeatures
    inp = ex.createInput(profs, op)
    inp.startExtent = adsk.fusion.OffsetStartDefinition.create(V(z0))
    inp.setDistanceExtent(False, V(z1 - z0))
    if body is not None:
        inp.participantBodies = [body]
    f = ex.add(inp)
    return f.bodies.item(0) if f.bodies.count else body

NEU = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
DAZU = adsk.fusion.FeatureOperations.JoinFeatureOperation
WEG = adsk.fusion.FeatureOperations.CutFeatureOperation

def box(comp, x0, y0, x1, y1, z0, z1, op, body=None, r=0.0):
    sk = skizze(comp); rrect(sk, x0, y0, x1, y1, r)
    return extr(comp, sk, z0, z1, op, body)

def zyl(comp, x, y, d, z0, z1, op, body=None):
    sk = skizze(comp); kreis(sk, x, y, d)
    return extr(comp, sk, z0, z1, op, body)

def neue_komponente(parent, name):
    occ = parent.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    occ.component.name = name
    return occ.component

def gehaeuse(root):
    c = neue_komponente(root, 'Gehaeuse')
    b = box(c, AX0, AY0, AX1, AY1, 0, Z_DI, NEU, r=R_AUSSEN)
    b.name = 'Gehaeuse'
    box(c, IX0, IY0, IX1, IY1, FRONT, Z_DI, WEG, b, r=R_INNEN)          # Innenraum
    # Displayfenster mit Stufe aussen
    x0, y0, x1, y1 = FENSTER
    box(c, x0, y0, x1, y1, 0, FRONT, WEG, b, r=0.5)
    box(c, x0 - 0.8, y0 - 0.8, x1 + 0.8, y1 + 0.8, 0, 0.6, WEG, b, r=1.0)
    # Lautsprecher-Tasche von innen + Schalloecher
    zyl(c, LS[0], LS[1], LS_D + 0.4, 0.8, FRONT, WEG, b)
    sk = skizze(c)
    kreis(sk, LS[0], LS[1], 1.0)
    for ring, n in ((2.4, 6), (4.5, 12)):
        for k in range(n):
            a = 2 * math.pi * k / n
            kreis(sk, LS[0] + ring * math.cos(a), LS[1] + ring * math.sin(a), 1.0)
    extr(c, sk, 0, 0.8, WEG, b)
    # Taster: Loch fuer Stoessel + Senkung innen fuer den Kragen
    for x, y in TASTER:
        zyl(c, x, y, 4.3, 0, FRONT, WEG, b)
        zyl(c, x, y, 6.6, FRONT - 0.7, FRONT, WEG, b)
    # Abstandshalter unten (Schraube von vorne) mit Senkung fuer den Schraubenkopf
    for x, y in LOCH_UNTEN:
        zyl(c, x, y, 4.6, FRONT, Z_PV, DAZU, b)
        zyl(c, x, y, 2.3, 0, Z_PV, WEG, b)
        zyl(c, x, y, 4.2, 0, 1.4, WEG, b)
    # USB-Oeffnung unten
    zu = Z_PH + XIAO_D + USB_H / 2
    box(c, USB_X - (USB_B + 0.7) / 2, AY0 - 0.5, USB_X + (USB_B + 0.7) / 2, IY0 + 0.5,
        zu - (USB_H + 0.8) / 2, zu + (USB_H + 0.8) / 2, WEG, b)
    # Schraubblock oben fuer die Deckelschraube
    sx, sy = SCHRAUBE_OBEN
    box(c, sx - 3.0, PL_L - 0.7, sx + 3.0, IY1 + 0.5, Z_DI - 6.0, Z_DI, DAZU, b)
    zyl(c, sx, sy, 1.7, Z_DI - 5.6, Z_DI, WEG, b)
    return c

def deckel(root):
    c = neue_komponente(root, 'Deckel')
    b = box(c, AX0, AY0, AX1, AY1, Z_DI, Z_DA, NEU, r=R_AUSSEN)
    b.name = 'Deckel'
    # Zentrierrand, der innen ins Gehaeuse greift
    box(c, IX0 + 0.2, IY0 + 0.2, IX1 - 0.2, IY1 - 0.2, Z_DI - 0.8, Z_DI, DAZU, b, r=R_INNEN)
    box(c, IX0 + 1.2, IY0 + 1.2, IX1 - 1.2, IY1 - 1.2, Z_DI - 0.8, Z_DI, WEG, b, r=0.5)
    sx, sy = SCHRAUBE_OBEN
    box(c, sx - 3.4, IY1 - 1.5, sx + 3.4, IY1, Z_DI - 0.8, Z_DI, WEG, b)    # Platz fuer Schraubblock
    # Pfosten unten: druecken auf die Platine, Schraube von vorne greift hier
    for x, y in LOCH_UNTEN:
        zyl(c, x, y, 4.6, Z_PH, Z_DI, DAZU, b)
        zyl(c, x, y, 1.7, Z_PH, Z_DI - 0.5, WEG, b)
    # Pfosten oben mit Zentrierstift ins Platinenloch
    for x, y in LOCH_OBEN:
        zyl(c, x, y, 4.0, Z_PH, Z_DI, DAZU, b)
        zyl(c, x, y, 1.9, Z_PV + 0.2, Z_PH, DAZU, b)
    # Deckelschraube oben
    zyl(c, sx, sy, 2.3, Z_DI, Z_DA, WEG, b)
    zyl(c, sx, sy, 4.2, Z_DA - 1.4, Z_DA, WEG, b)
    return c

def stoessel(root, nr, x, y):
    c = neue_komponente(root, 'Taster_Stoessel_%d' % nr)
    b = zyl(c, x, y, 4.0, -0.8, Z_PV - TASTER_H - 0.05, NEU)
    zyl(c, x, y, 6.2, FRONT - 0.6, FRONT - 0.1, DAZU, b)
    b.name = 'Stoessel_%d' % nr
    return c

def referenzen(root):
    r = neue_komponente(root, 'Referenz (nicht drucken)')
    def teil(name, fn):
        k = neue_komponente(r, name); bb = fn(k); bb.name = name
    teil('Platine', lambda k: box(k, 0, 0, PL_B, PL_L, Z_PV, Z_PH, NEU, r=1.0))
    teil('Displayglas', lambda k: box(k, GLAS[0], GLAS[1], GLAS[2], GLAS[3], FRONT, FRONT + GLAS_D, NEU))
    teil('Schaumstoff', lambda k: box(k, AKKU[0], AKKU[1], AKKU[2], AKKU[3], Z_PH, Z_PH + SCHAUM, NEU))
    teil('Akku_300mAh', lambda k: box(k, AKKU[0], AKKU[1], AKKU[2], AKKU[3], Z_PH + SCHAUM, Z_PH + SCHAUM + AKKU_D, NEU, r=1.0))
    teil('XIAO', lambda k: box(k, PL_B / 2 - 8.9, 0, PL_B / 2 + 8.9, 21.0, Z_PH, Z_PH + XIAO_D, NEU))
    teil('USB_Buchse', lambda k: box(k, USB_X - USB_B / 2, -1.5, USB_X + USB_B / 2, 6.0, Z_PH + XIAO_D, Z_PH + XIAO_D + USB_H, NEU))
    teil('Lautsprecher', lambda k: zyl(k, LS[0], LS[1], LS_D, 0.8, Z_PV, NEU))
    for i, (x, y) in enumerate(TASTER):
        teil('Taster_%d' % (i + 1), lambda k, x=x, y=y: box(k, x - 2.55, y - 2.55, x + 2.55, y + 2.55, Z_PV - TASTER_H, Z_PV, NEU))

def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        root = design.rootComponent
        gehaeuse(root)
        deckel(root)
        for i, (x, y) in enumerate(TASTER):
            stoessel(root, i + 1, x, y)
        referenzen(root)
        app.activeViewport.fit()
        ui.messageBox('Sweef Gehaeuse v1 fertig.\n\nAussenmass: %.1f x %.1f x %.1f mm'
                      % (AX1 - AX0, AY1 - AY0, Z_DA))
    except:
        if ui:
            ui.messageBox('Fehler:\n{}'.format(traceback.format_exc()))
