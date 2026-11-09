"""Validate an inventory event against the direction of travel at a new site."""
import argparse,json,os
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for arg in ('work','site'):
        parser.add_argument(arg,type=Path)
    parser.add_argument('key')
    for arg in ('lat','lon','heading'):
        parser.add_argument(arg,type=float)
    args=parser.parse_args()
    os.environ['SD_SECTION_WORK']=str(args.work.resolve())
    from evaluate import profile
    point=[args.lat,args.lon,args.heading]
    result=profile(args.key,point)
    if result.get('status')!='matched':
        raise SystemExit(result.get('status','no match'))
    args.site.mkdir(parents=True,exist_ok=True)
    (args.site/'selection.json').write_text(json.dumps(dict(key=args.key,center=point,result=result),indent=2))

if __name__=='__main__':main()
