"""Prepare mapped protected areas for optional road crossfall. Requires shapely.

python prepare.py BASE_TILE.json CACHED_Z14_VECTOR OUTPUT.json
The game never calls this municipality-independent offline tool while driving.
"""
import argparse
import gzip
import hashlib
import json
import math
from datetime import date
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import Request, urlopen
from shapely.geometry import LineString, Point

EARTH = 40075016.68557849
BASE = 'https://services6.arcgis.com/Do88DoK2xjTUCXd1/ArcGIS/rest/services/OSM_NA_'
PAVED = {'motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'minor'}
ROAD_TAGS = {'motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'residential', 'unclassified','living_street'}
MAX_BODY = 420000


def sources(tile, side):
    x,y=tile
    width=EARTH/65536
    west=x*width-EARTH/2
    north=EARTH/2-y*width
    bounds=[west-28,north-width-28,west+width+28,north+28]
    output=[]
    sources=[('Highways','osm_id2,name,highway,bridge,tunnel,layer,footway,crossing,junction,surface','1=1'),
             ('POIs','osm_id2,highway,crossing',"highway IN ('crossing','traffic_signals','stop') OR crossing IS NOT NULL")]
    for layer,fields,where in sources:
        args={'f':'json','geometryType':'esriGeometryEnvelope','geometry':','.join(map(str,bounds)),
              'inSR':3857,'outSR':3857,'outFields':fields,'where':where,'returnGeometry':'true',
              'geometryPrecision':2,'maxAllowableOffset':.15,'resultRecordCount':700}
        url=BASE+layer+'/FeatureServer/0/query?'+urlencode(args)
        with urlopen(Request(url,headers={'User-Agent':'SundayDriver-offline-road-research/1'}),timeout=8) as response:
            raw=response.read(MAX_BODY+1)
        data=json.loads(raw)
        if len(raw)>MAX_BODY or data.get('exceededTransferLimit') or not isinstance(data.get('features'),list):
            raise ValueError('Incomplete public roadway control source')
        for feature in data['features']:
            geometry=feature['geometry']
            attrs=feature['attributes']
            ident=attrs.pop('osm_id2',None)
            if not ident or not str(ident).isdigit():continue
            lines=geometry['paths'] if layer=='Highways' else [[[geometry['x'],geometry['y']]]]
            for line in lines:
                xy=[[round((px-west)*side/width,3),round((north-py)*side/width,3)] for px,py in line]
                output.append({'id':str(ident),'xy':xy,'tags':{k:v for k,v in attrs.items() if v not in [None,'']}})
    return output


def prepare(tile, raw_vector, items):
    x,y=tile['id']
    side=EARTH/65536/math.cosh(math.pi*(1-2*(y+.5)/65536))
    ways=[(r['id'],r['tags'],LineString(r['xy'])) for r in items if len(r['xy'])>1]
    stops=[Point(r['xy'][0]) for r in items if len(r['xy'])==1]
    series=[]
    for i,road in enumerate(tile['roads']):
        cls,one,name,uv,width,*flags=road
        half=min(width/2-.8,5)
        if cls not in PAVED or not name or 'unpaved' in flags or half<2.25:continue
        line=LineString([(u*side,v*side) for u,v in uv])
        for s in range(12,math.ceil(line.length-11),6):
            p=line.interpolate(s)
            if min(p.x,p.y,side-p.x,side-p.y)<17:continue
            ahead=line.interpolate(s+3);back=line.interpolate(s-3)
            length=ahead.distance(back)
            if length<5.8:continue
            tx=(ahead.x-back.x)/length;ty=(ahead.y-back.y)/length
            if any(line.interpolate(s+d).distance(Point(p.x+d*tx,p.y+d*ty))>.35 for d in [-6,6]):continue
            if any(q.distance(p)<14 for q in stops):continue
            nearby=[(key,t,g,g.distance(p)) for key,t,g in ways if g.distance(p)<15]
            if any(t.get('bridge') not in (None,'no') or t.get('tunnel') not in (None,'no')
                   or str(t.get('layer',0)) not in ('0','0.0') or t.get('junction') in ('roundabout','circular')
                   or ((t.get('crossing') or t.get('footway')=='crossing') and d<14) for _,t,_,d in nearby):continue
            matches=[]
            for ident,t,g,d in nearby:
                if t.get('highway') not in ROAD_TAGS or t.get('name','').casefold()!=name.casefold() or d>=1.7:continue
                dist=g.project(p)
                if min(dist,g.length-dist)<11:continue
                left=g.interpolate(dist-2);right=g.interpolate(dist+2);distance=left.distance(right)
                if distance and abs(tx*(right.x-left.x)+ty*(right.y-left.y))/distance>.992:matches.append((ident,t))
            if len(matches)!=1:continue
            ident,t=matches[0]
            if (t.get('surface') or '').lower() not in ('','asphalt','paved','concrete','concrete:plates','paving_stones'):continue
            if any(key!=ident and d<(7 if tag.get('highway') in ('service','footway','path') else 13) for key,tag,_,d in nearby):continue
            series.append([i,ident,s,round(p.x,3),round(p.y,3),round(tx,6),round(ty,6),round(half,3)])
    if len(series)>768:raise ValueError('Road registry exceeds tile budget')
    rows=['%d|%s'%(round(r[4]*100),','.join('%d/%d'%(round(p[0]*1000000),round(p[1]*1000000)) for p in r[3])) for r in tile['roads']]
    raw=gzip.decompress(raw_vector) if raw_vector[:2]==b'\x1f\x8b' else raw_vector
    return {'id':[x,y],'prepared':date.today().isoformat(),'roadSignature':hashlib.sha256('\n'.join(sorted(rows)).encode()).hexdigest(),
            'mapFingerprint':hashlib.sha256(raw).hexdigest()[:24],'sections':series}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tile',type=Path);parser.add_argument('vector',type=Path);parser.add_argument('out',type=Path)
    args=parser.parse_args();tile=json.loads(args.tile.read_text());x,y=tile['id'];side=EARTH/65536/math.cosh(math.pi*(1-2*(y+.5)/65536))
    print('Preparing independently bounded road and traffic-control records for',tile['id'])
    record=prepare(tile,args.vector.read_bytes(),sources(tile['id'],side))
    args.out.write_text(json.dumps(record,separators=(',',':'))+'\n')
