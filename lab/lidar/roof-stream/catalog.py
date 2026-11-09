"""Refresh the free EPT mirror's published directory names without listing tile objects."""
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import urlopen,Request
from xml.etree import ElementTree as ET
import json,re,sys
target=Path(sys.argv[1]);names=[];token=None;namespace={'s':'http://s3.amazonaws.com/doc/2006-03-01/'}
while True:
 params={'list-type':'2','delimiter':'/','max-keys':'1000'}
 if token:params['continuation-token']=token
 url='https://usgs-lidar-public.s3.amazonaws.com/?'+urlencode(params)
 with urlopen(Request(url,headers={'User-Agent':'SundayDriver/1 survey index authoring'}),timeout=15) as r:
  page=ET.fromstring(r.read(750000))
 for element in page.findall('s:CommonPrefixes/s:Prefix',namespace):
  value=element.text.rstrip('/')
  if '_' in value and re.fullmatch(r'[-A-Za-z0-9_]+',value):names.append(value)
 if page.findtext('s:IsTruncated','false',namespace)=='false':break
 token=page.findtext('s:NextContinuationToken',None,namespace)
 if not token:raise ValueError('Incomplete S3 listing')
if len(names)<1500:raise ValueError('Incomplete EPT directory')
target.write_text(json.dumps(sorted(set(names)),separators=(',',':'))+'\n')
print(len(set(names)),'published directories')
