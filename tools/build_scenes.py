#!/usr/bin/env python3
"""Scene library: Bible illustrations pulled from Wikimedia Commons, dithered for the panel.

  scenes/david-and-goliath.md       one file per scene (front matter: title, commons, source, passage, characters)
  build_scenes.py                   -> scenes/out/<id>.img (1-bit, zlib) + scenes/out/index.json

Image format: 4 bytes little-endian width and height (uint16 each), then a zlib stream of the
packed 1-bit rows (MSB first, 1 = ink). Plates are fit to 456 px wide, at most 300 px tall.
Sources: Schnorr von Carolsfeld (1860) and Gustave Dore (1866) are public domain; Sweet Publishing
illustrations are CC BY-SA 3.0 and carry a credit line.
"""
import hashlib, io, json, pathlib, re, struct, sys, urllib.parse, urllib.request, zlib
from PIL import Image, ImageOps, ImageFilter, ImageEnhance
ROOT=pathlib.Path(__file__).resolve().parent.parent
SRC=ROOT/'scenes';OUT=SRC/'out';CACHE=ROOT/'.tools/scenes'
UA={'User-Agent':'Pockle/1.0 (e-paper devotional; https://github.com/WhatsNewSaes/pockle)'}
MAXW,MAXH=456,300
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
  print('downloading',commons);f.write_bytes(urllib.request.urlopen(urllib.request.Request(url,headers=UA),timeout=120).read())
 return Image.open(io.BytesIO(f.read_bytes()))
def dither(im,crop=None):
 im=im.convert('L')
 if crop:  # fractions: left, top, right, bottom
  l,t,r,b=[float(x) for x in crop.split(',')];w,h=im.size;im=im.crop((int(w*l),int(h*t),int(w*r),int(h*b)))
 w,h=im.size;scale=min(MAXW/w,MAXH/h);im=im.resize((max(1,int(w*scale)),max(1,int(h*scale))),Image.Resampling.LANCZOS)
 # Painted sources dither into noise; a light blur and a contrast lift before Floyd-Steinberg keep the figures crisp and calm the skies.
 im=ImageOps.autocontrast(im.filter(ImageFilter.GaussianBlur(0.8)),cutoff=2);im=ImageEnhance.Contrast(im).enhance(1.3)
 return im.convert('1')
def pack(im):
 w,h=im.size;px=im.load();rows=bytearray()
 for y in range(h):
  acc=0;n=0
  for x in range(w):
   acc=(acc<<1)|(0 if px[x,y] else 1);n+=1  # ink = 1
   if n==8:rows.append(acc);acc=0;n=0
  if n:rows.append(acc<<(8-n))
 return struct.pack('<HH',w,h)+zlib.compress(bytes(rows),9)
def main():
 OUT.mkdir(parents=True,exist_ok=True);index={}
 for f in sorted(SRC.glob('*.md')):
  meta,caption=front(f);sid=f.stem
  im=dither(fetch(meta['commons']),meta.get('crop'));data=pack(im)
  (OUT/f'{sid}.img').write_bytes(data);(OUT/f'{sid}.png').write_bytes(b'');im.save(OUT/f'{sid}.png')
  index[sid]={'title':meta.get('title',sid),'source':meta.get('source',''),'credit':CREDITS.get(meta.get('source',''),meta.get('source','')),'passage':meta.get('passage',''),'characters':[c.strip() for c in meta.get('characters','').split(',') if c.strip()],'caption':caption,'w':im.size[0],'h':im.size[1],'bytes':len(data),'hash':hashlib.sha1(data).hexdigest()[:8]}
  print(f'{sid}: {im.size[0]}x{im.size[1]} {len(data)/1024:.1f} KB ({meta.get("source")})')
 (OUT/'index.json').write_text(json.dumps(index,indent=1))
 print('scenes:',len(index))
if __name__=='__main__':main()
