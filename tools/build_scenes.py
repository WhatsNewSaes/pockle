#!/usr/bin/env python3
"""Scene library: Bible illustrations pulled from Wikimedia Commons, dithered for the panel.

  scenes/library.json               the Sweet Publishing plates tools/pick_scenes.py chose (one per day, plus the characters')
  scenes/<id>.md                    hand-picked extras (front matter: title, commons, source, passage, characters; optional crop)
  build_scenes.py                   -> scenes/out/<id>.img + scenes/out/index.json; previews in .tools/scenes/preview/

Image format (version 2): width and height as little-endian uint16, a uint16 text length, that many
bytes of text "credit<US>passage<US>caption" (US = 0x1f), then a zlib stream of the packed 1-bit rows
(MSB first, 1 = ink). Plates are fit to 456 px wide, at most 300 px tall.
Sources: Schnorr von Carolsfeld (1860) and Gustave Dore (1866) are public domain; Sweet Publishing
illustrations are CC BY-SA 3.0 and carry a credit line.
"""
import concurrent.futures, hashlib, io, json, pathlib, re, struct, sys, time, urllib.parse, urllib.request, zlib
from PIL import Image, ImageOps, ImageFilter, ImageEnhance
ROOT=pathlib.Path(__file__).resolve().parent.parent
SRC=ROOT/'scenes';OUT=SRC/'out';CACHE=ROOT/'.tools/scenes';PREVIEW=CACHE/'preview'
UA={'User-Agent':'Pockle/1.0 (e-paper devotional; https://github.com/WhatsNewSaes/pockle)'}
MAXW,MAXH=456,300;US='\x1f'
CREDITS={'schnorr':'Julius Schnorr von Carolsfeld, 1860 (public domain)','dore':'Gustave Dore, 1866 (public domain)','sweet':'Sweet Publishing, CC BY-SA 3.0'}
def front(path):
 text=path.read_text();m=re.match(r'^---\n(.*?)\n---\n(.*)$',text,re.S)
 if not m:sys.exit(f'{path.name}: missing front matter')
 meta={}
 for line in m.group(1).split('\n'):
  if ':' in line:k,v=line.split(':',1);meta[k.strip()]=v.strip()
 return meta,m.group(2).strip()
def fetch(commons):
 CACHE.mkdir(parents=True,exist_ok=True);key=hashlib.sha1(commons.encode()).hexdigest()[:12];f=CACHE/f'{key}.img'
 if not f.exists():
  url='https://commons.wikimedia.org/wiki/Special:FilePath/'+urllib.parse.quote(commons.replace('File:',''))+'?width=1000'
  for a in range(8):  # Commons rate-limits bots (429): back off and try again
   try:f.write_bytes(urllib.request.urlopen(urllib.request.Request(url,headers=UA),timeout=120).read());time.sleep(0.4);break
   except Exception as e:
    if a==7:raise
    time.sleep(15 if '429' in str(e) else 3+a*5)
 return Image.open(io.BytesIO(f.read_bytes()))
def dither(im,crop=None):
 im=im.convert('L')
 if crop:  # fractions: left, top, right, bottom
  l,t,r,b=[float(x) for x in crop.split(',')];w,h=im.size;im=im.crop((int(w*l),int(h*t),int(w*r),int(h*b)))
 w,h=im.size;scale=min(MAXW/w,MAXH/h);im=im.resize((max(1,int(w*scale)),max(1,int(h*scale))),Image.Resampling.LANCZOS)
 # Painted sources dither into noise; a light blur and a contrast lift before Floyd-Steinberg keep the figures crisp and calm the skies.
 im=ImageOps.autocontrast(im.filter(ImageFilter.GaussianBlur(0.8)),cutoff=2);im=ImageEnhance.Contrast(im).enhance(1.3)
 return im.convert('1')
def tidy(caption):
 # The captions often end by naming the chapter ("..., as described in Luke 18."); the card and the sleep screen already show the passage.
 c=re.sub(r',?\s*(as\s+(described|told|seen|depicted|recorded|mentioned|narrated|written)\s+)?(in|from)\s+(the\s+)?(book\s+of\s+|gospel\s+of\s+)?(\d\s+)?[A-Z][a-z]+(\s+chapter)?\s+\d+(:\d+(-\d+)?)?\s*\.?$','',caption.strip(),flags=re.I).strip()
 if c and c.rstrip('"\'')[-1:] not in ('.','!','?'):c+='.'
 return c
def ascii_(s):
 for k,v in {'’':"'",'‘':"'",'“':'"','”':'"','—':' - ','…':'...','é':'e'}.items():s=s.replace(k,v)
 return ''.join(c if 32<=ord(c)<127 else '?' for c in s)
def pack(im,text):
 w,h=im.size;px=im.load();rows=bytearray()
 for y in range(h):
  acc=0;n=0
  for x in range(w):
   acc=(acc<<1)|(0 if px[x,y] else 1);n+=1  # ink = 1
   if n==8:rows.append(acc);acc=0;n=0
  if n:rows.append(acc<<(8-n))
 t=US.join(ascii_(part) for part in text.split(US)).encode()[:255];return struct.pack('<HHH',w,h,len(t))+t+zlib.compress(bytes(rows),9)  # ASCII per part: the separators must survive
def build(sid,meta,caption):
 caption=tidy(caption);im=dither(fetch(meta['commons']),meta.get('crop'));credit=CREDITS.get(meta.get('source',''),meta.get('credit',meta.get('source','')))
 data=pack(im,US.join([credit,meta.get('passage',''),caption]))
 (OUT/f'{sid}.img').write_bytes(data);im.save(PREVIEW/f'{sid}.png')
 return {'title':meta.get('title',caption or sid),'source':meta.get('source','sweet'),'credit':credit,'passage':meta.get('passage',''),'characters':[c.strip() for c in meta.get('characters','').split(',') if c.strip()],'caption':caption,'w':im.size[0],'h':im.size[1],'bytes':len(data),'hash':hashlib.sha1(data).hexdigest()[:8]}
def main():
 OUT.mkdir(parents=True,exist_ok=True);PREVIEW.mkdir(parents=True,exist_ok=True);jobs={}
 lib=json.loads((SRC/'library.json').read_text()) if (SRC/'library.json').exists() else {}
 for sid,e in lib.items():jobs[sid]=({'commons':e['commons'],'source':'sweet','passage':e.get('passage',''),'characters':', '.join(e.get('people',[]))},e.get('caption',''))
 for f in sorted(SRC.glob('*.md')):meta,caption=front(f);jobs[f.stem]=(meta,caption)
 for old in OUT.glob('*.img'):
  if old.stem not in jobs:old.unlink()
 for old in OUT.glob('*.png'):old.unlink()
 index={};total=0
 with concurrent.futures.ThreadPoolExecutor(2) as ex:  # two at a time keeps Commons happy
  for sid,entry in zip(jobs,ex.map(lambda kv:build(kv[0],*kv[1]),jobs.items())):index[sid]=entry;total+=entry['bytes']
 (OUT/'index.json').write_text(json.dumps(index,indent=1))
 print(f'scenes: {len(index)}, {total/1048576:.2f} MB ({total/max(1,len(index))/1024:.1f} KB each)')
if __name__=='__main__':main()
