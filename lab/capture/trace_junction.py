"""Apply Chula / US 360 survey annotations to a clean pavement tile.

Run with the original pave-18585-25424.png produced by lab/road_edges.py as
the argument. Measurements use the packaged Virginia Base Mapping Program
Most Recent Imagery orthophoto va-18585-25424.jpg (768 square, ~0.63 m/pixel).
Positions below are metres along/across a 107.33° true heading from
37.369103 N, 77.907503 W. Line widths are rendering assumptions: most
paint stripes are below the ground sampling distance. Individual visible
dash runs are stored here; arrows, parked vehicles and obscured paint
are omitted. The road polygons, forecourts and median noses follow the
visible color/texture boundary of this aerial acquisition. They do not
establish survey-grade boundary accuracy or a simultaneous street date.
"""
import argparse
import json
import math
from pathlib import Path
from PIL import Image, ImageDraw

TILE = (18585, 25424)
ROOT = Path(__file__).resolve().parents[2] / 'game' / 'data'
TILE_SIDE = 40075016.68557849 / 65536 * math.cos(math.atan(math.sinh(math.pi*(1-2*(TILE[1]+.5)/65536))))
REFERENCE = ((-77.907503+180)*65536/360-TILE[0],
             (1-math.asinh(math.tan(math.radians(37.369103)))/math.pi)*32768-TILE[1])
AX = math.sin(math.radians(107.33))
AY = -math.cos(math.radians(107.33))

def uv(p):
    along, across = p
    return [round(REFERENCE[0]+(along*AX-across*AY)/TILE_SIDE,7),
            round(REFERENCE[1]+(along*AY+across*AX)/TILE_SIDE,7)]

# Opaque classes follow the existing field shader and grip sampling. The high
# alpha of a sealed, pale forecourt preserves part of its photographed color.
ROAD = (255,0,0,127)
TURF = (0,255,0,127)
SHOULDER = (0,0,0,191)
COURT = (0,0,0,231)
REGIONS = [
 (ROAD, [(-76,-4.4),(-24,-4.6),(-13,-5.0),(-8,-6.1),(-4,-9.8),(15,-10.6),(24,-10.45),(57,-10.55),(72,-9.75),(82,-7.95),(97,-5.35),(109,-3.8),(116,-3.4),(116,4.75),(61,4.25),(17,3.7),(-8,3.45),(-46,3.15),(-76,3.12)]),
 (ROAD, [(-76,12.4),(-24,11.9),(-11,11.55),(-7,11.1),(-4.1,9.9),(13.2,10.5),(15.3,13.25),(19.5,14.5),(29,14.7),(83,15.0),(116,15.35),(116,23.0),(81,22.8),(29,22.45),(-8,22.3),(-76,22.0)]),
 (ROAD, [(-7.3,-38),(10.0,-38),(10.1,-25),(10.5,-20.3),(11.8,-16.3),(13.9,-13.6),(18.9,-11.35),(23.5,-10.4),(23.5,3.4),(16.5,4.0),(14.2,5.2),(13.4,6.7),(13.4,9.3),(14.55,12.3),(17.1,13.95),(21.7,14.9),(-14,15.6),(-13.7,11.7),(-8,11.2),(-4.3,9.6),(-2.25,7.65),(-2.3,6.0),(-4.15,4.35),(-7.9,3.5),(-12.7,-5.3),(-9.1,-7.4),(-7.4,-10.6),(-6.8,-14.9)]),
 (SHOULDER,[(-75,22.25),(-24,22.45),(9,22.58),(27,22.75),(27,24.05),(-10,24.03),(-49,24.45),(-75,24.6)]),
 (TURF,[(-76,3.65),(-45,3.55),(-14.5,3.85),(-8.15,4.15),(-4.4,4.95),(-3.3,6.1),(-3.3,7.55),(-5.0,9.0),(-8.5,10.6),(-15.2,11.3),(-46,11.9),(-76,11.95)]),
 (TURF,[(15.1,5.8),(17.0,4.72),(21.2,4.45),(61,4.75),(116,5.15),(116,14.90),(59,14.52),(26,14.05),(20.6,13.8),(17.25,13.0),(15.4,11.6),(14.3,9.7),(14.3,7.25)]),
 (TURF,[(-7.8,-37.8),(-7.9,-30.0),(-9.6,-26.0),(-14.1,-23.4),(-21.3,-20.8),(-29,-18.55),(-40,-17.3),(-48.7,-16.4),(-50.9,-14.15),(-49.1,-11.2),(-46,-8.7),(-20,-7.65),(-13.2,-8.2),(-9.0,-10.0),(-8.1,-13.9)]),
 (TURF,[(10.6,-38.0),(12.0,-36.1),(13.1,-29.2),(15.15,-24.75),(20.1,-20.7),(26.8,-17.5),(32.3,-16.4),(42.8,-16.45),(46.45,-16.1),(45.9,-12.4),(43.8,-11.6),(25.3,-11.65),(18.9,-12.6),(14.9,-14.8),(12.4,-18.1),(11.1,-23.8)]),
 (TURF,[(.0,-37.8),(3.9,-37.8),(4.25,-30.0),(4.24,-20.0),(4.5,-13.4),(4.3,-11.7),(1.6,-11.5),(.8,-13.1),(.3,-20.0)]),
 (COURT,[(-90.0,-45.4),(-84.2,-53.1),(-77.0,-54.1),(-46,-53.1),(-45.3,-63.6),(-39.4,-63.3),(-34,-62.6),(-23,-60.85),(-11.2,-58.9),(-9.0,-56),(-9.9,-36.0),(-11.6,-30.6),(-15.3,-26.5),(-23,-23.1),(-34.7,-19.4),(-46.7,-18.0),(-51.5,-17.3),(-52.7,-14.6),(-52.3,-10.0),(-49.2,-7.2),(-63.7,-7.4),(-63.0,-10.8),(-63.1,-14.0),(-64.9,-17.0),(-69.9,-18.9),(-78.3,-18.6),(-89.4,-17.0)]),
 (COURT,[(12,-53.6),(15,-56.7),(24.7,-58.2),(31.5,-56.3),(33.4,-52.4),(52,-51.9),(57.9,-54.4),(61.3,-50.3),(63.2,-44.5),(63.9,-25.9),(64.25,-16.6),(63.55,-11.1),(46.8,-11.4),(47.0,-16.3),(44.6,-17.45),(31.2,-17.6),(26.6,-19.1),(21.4,-21.6),(16.1,-26.4),(14.4,-30),(13.6,-39.7),(12.5,-47.0)])
]

EXTENTS = [[(-74,-11.7),(113,-11.7),(113,24.0),(-74,24.0)],
           [(-7.65,-37.9),(11,-37.9),(11,-11.8),(-7.65,-11.8)]]

# Continuous visible pavement stripes and stop bars. Unreadable or
# vehicle-occluded sections are left open (including the articulated truck).
LINES = [
 ('white',.13,[(-74,-4.1),(-29,-4.2),(-18,-4.15)]),
 ('yellow',.13,[(-74,2.98),(-45,3.0),(-18,3.15),(-8.7,3.17)]),
 ('white',.14,[(18.8,-10.28),(42,-10.35),(59,-10.35),(71,-9.42),(83,-7.65),(97,-5.14),(109,-3.60),(113,-3.45)]),
 ('yellow',.13,[(18,3.48),(60,3.72),(93,4.08),(113,4.43)]),
 ('white',.14,[(17.45,-4.26),(31.0,-4.27)]),
 ('white',.14,[(43.45,-4.26),(58.1,-4.21)]),
 ('yellow',.14,[(-74,12.55),(-55,12.25),(-27,12.1),(-10.8,11.86)]),
 ('white',.13,[(-74,21.61),(-30.0,21.70),(-6,21.87)]),
 ('white',.13,[(14.2,22.1),(28.1,22.12)]),
 ('white',.14,[(-50.4,15.68),(-23,15.61),(-7.5,15.34)]),
 ('white',.14,[(-46.6,19.14),(-24.8,19.12),(-6.2,19.13)]),
 ('yellow',.14,[(19.9,14.91),(32.1,15.00),(51.2,15.02)]),
 ('white',.40,[(16.62,-10.05),(17.05,-4.7)]),
 ('white',.40,[(17.18,1.82),(17.54,3.65)]),
 ('white',.43,[(-5.72,11.83),(-5.15,21.79)]),
 ('white',.39,[(-6.4,-10.64),(.55,-10.68)]),
]
DASHES = [
 (-62,-59.5,-.2),(-51,-47.2,-.2),(-38,-34.8,-.3),(-26.2,-22.7,-.3),(-14.2,-10.5,-.5),
 (35.8,39.5,-.1),(47.5,51.5,-.1),(59.8,62.5,-.0),(71.8,74.8,.2),(84.,87.,.3),(96.,99.5,.5),(108.,111.5,.7),
 (-67,-64.5,19.1),(-51.5,-48.2,19.2),(19.2,22.2,18.9),(32.2,34.2,18.8),(42.5,46.5,18.9),(55.2,58.2,18.9),
 (67.5,70.2,19.1),(78.5,82.2,19.1),(91.5,94.2,19.4)]
LINES += [('white',.14,[(start,y),(end,y)]) for start,end,y in DASHES]

def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('clean_mask',type=Path)
    args=parser.parse_args()
    dest=ROOT/'pave-18585-25424.png'
    if args.clean_mask.resolve()==dest.resolve():
        raise SystemExit('Supply a distinct clean input to avoid re-applying antialiasing.')
    base=Image.open(args.clean_mask).convert('RGBA')
    scale=4
    n=base.width*scale
    # Only blend the antialiased polygon footprints, leaving all other
    # original RGBA samples byte-identical (alpha is a class channel).
    for value,points in REGIONS:
        mask=Image.new('L',(n,n))
        ImageDraw.Draw(mask).polygon([(round(x*n),round(y*n)) for x,y in map(uv,points)],fill=255)
        blended=mask.resize(base.size,Image.Resampling.BOX)
        base.paste(value,(0,0,base.width,base.height),blended)
    base.save(dest,optimize=True)
    overlay={'extent':[[uv(p) for p in zone] for zone in EXTENTS],
             'lines':[{'ink':kind,'width_m':width,'uv':[uv(p) for p in pts]} for kind,width,pts in LINES]}
    (ROOT/'markings-18585-25424.json').write_text(json.dumps(overlay,separators=(',',':'))+'\n')

if __name__=='__main__':
    main()
