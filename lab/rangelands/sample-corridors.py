import requests,json,random
from pathlib import Path
D=Path(__file__).parent
sites={'US 50, Nevada':(39.48855,-116.79696),'CA 62, California':(34.1319,-116.0172),'US 70, New Mexico':(32.7731,-106.1810),'US 191, Utah':(38.1146,-109.4000),'Badlands Loop, South Dakota':(43.7577,-101.9505),'AZ 64, Arizona':(35.9083,-111.7270)}
s=list(sites.items());random.Random(19990515).shuffle(s)
H={'User-Agent':'SundayDriverResearch/1.0 (noncommercial open-source photographic comparison)'}
for name,(lat,lon) in s[:3]:
 p={'action':'query','generator':'geosearch','ggsnamespace':6,'ggscoord':f'{lat}|{lon}','ggsradius':4000,'ggslimit':30,'prop':'coordinates|imageinfo','iiprop':'extmetadata|metadata|url','format':'json'}
 d=requests.get('https://commons.wikimedia.org/w/api.php',params=p,headers=H,timeout=7).json()
 (D/(name.split(',')[0].replace(' ','_')+'.json')).write_text(json.dumps(d))
 print(name,[(x.get('pageid'),x['title'][:85]) for x in d.get('query',{}).get('pages',{}).values()][:11],flush=True)
