#!/usr/bin/env python3
"""Bible characters: one Markdown file each, exported to the index the device reads.

  characters/david.md:
    ---
    name: David
    scene: david-and-goliath          # a scene id from scenes/
    passages: 1 Samuel 17, Psalm 23   # what to read, in order
    chapters: 1 Samuel 16, 1 Samuel 17 # where draft_characters.py may pick the plate from
    order: 28                         # place in the list (story order); drafted by tools/draft_characters.py
    ---
    Two or three short sentences for a 7-13 year old.

    - Three bullets to remember
    - ...
    - ...
"""
import json, pathlib, re, sys
ROOT=pathlib.Path(__file__).resolve().parent.parent
SRC=ROOT/'characters';OUT=SRC/'out'
def main():
 OUT.mkdir(parents=True,exist_ok=True);scenes=json.loads((ROOT/'scenes/out/index.json').read_text()) if (ROOT/'scenes/out/index.json').exists() else {}
 out=[]
 for f in sorted(SRC.glob('*.md')):
  text=f.read_text();m=re.match(r'^---\n(.*?)\n---\n(.*)$',text,re.S)
  if not m:sys.exit(f'{f.name}: missing front matter')
  meta={}
  for line in m.group(1).split('\n'):
   if ':' in line:k,v=line.split(':',1);meta[k.strip()]=v.split('#')[0].strip()
  body=m.group(2);bullets=[l.strip()[1:].strip() for l in body.split('\n') if l.strip().startswith('-')]
  blurb=' '.join(l.strip() for l in body.split('\n') if l.strip() and not l.strip().startswith('-'))
  for k,v in {'’':"'",'“':'"','”':'"','—':' - '}.items():blurb=blurb.replace(k,v);bullets=[b.replace(k,v) for b in bullets]
  if meta.get('scene') and meta['scene'] not in scenes:sys.exit(f'{f.name}: unknown scene {meta["scene"]}')
  if len(blurb)>420:sys.exit(f'{f.name}: blurb is {len(blurb)} characters (max 420)')
  out.append({'order':int(meta.get('order','999')),'id':f.stem,'name':meta.get('name',f.stem.title()),'scene':meta.get('scene',''),'passages':[p.strip() for p in meta.get('passages','').split(',') if p.strip()],'blurb':blurb,'bullets':bullets[:4]})
 out.sort(key=lambda c:(c['order'],c['name']));[c.pop('order') for c in out]
 (OUT/'index.json').write_text(json.dumps(out,indent=1));print('characters:',', '.join(c['name'] for c in out))
if __name__=='__main__':main()
