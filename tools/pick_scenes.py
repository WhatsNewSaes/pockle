#!/usr/bin/env python3
"""Choose a Sweet Publishing plate for every day's verse, and collect every plate the device needs.

  pick_scenes.py            # -> scenes/days.json
  pick_scenes.py --cached   # table days picked so far keep their plate, the rest get none for now; devotional files are still picked (one plate per day of the year + per devotional file) and scenes/library.json

Day plates: for each verse-of-the-day entry (Bible.h) the model picks the best-fitting
plate from the verse's own chapter, or from the stories beside it (sweet.POOL), never reusing a plate,
so every day of the year has its own picture. Devotional files with their own `ref` get a pick too
(their `scene:` front matter wins). Picks are cached in .tools/sweet/picks.json; delete an entry to redo it.
library.json then lists every plate referenced by days.json and characters/*.md for build_scenes.py.
"""
import argparse, datetime, json, pathlib, re, sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from sweet import Library, pick_for_verse, CODES
from biblelib import BOOKS, parse_ref
ROOT=pathlib.Path(__file__).resolve().parent.parent;OUT=ROOT/'scenes';PICKS=ROOT/'.tools/sweet/picks.json'
def votd():
 src=(ROOT/'firmware/RetroSports/Bible.h').read_text();table=re.search(r'verseOfDayTable\[\]\s*=\s*\{(.*?)\};',src,re.S).group(1)
 return [(int(m.group(1)),int(m.group(2)),int(m.group(3))) for m in re.finditer(r'\{(\d+),(\d+),(\d+)\}',table)]
def front(path):
 m=re.match(r'^---\n(.*?)\n---\n',path.read_text(),re.S);meta={}
 if m:
  for line in m.group(1).split('\n'):
   if ':' in line:k,v=line.split(':',1);meta[k.strip()]=v.split('#')[0].strip()
 return meta
def main():
 a=argparse.ArgumentParser();a.add_argument('--cached',action='store_true');a=a.parse_args()
 lib=Library();picks=json.loads(PICKS.read_text()) if PICKS.exists() else {};used=set();table=[]
 refs=votd()  # the device cycles this table by day of year, so days beyond its length repeat its first verses and their plates
 for i,(b,c,v) in enumerate(refs):
  key=f'table:{i}';sid=picks.get(key)
  if sid not in lib.plates or sid in used:
   if a.cached:table.append('');continue
   sid=pick_for_verse(lib,b,c,v,used);picks[key]=sid;PICKS.write_text(json.dumps(picks,indent=0));print(f'day {i+1:3d} {BOOKS[b-1][0]} {c}:{v} -> {sid}: {lib.plates[sid]["caption"]}',flush=True)
  used.add(sid);table.append(sid)
 files={}
 for f in sorted((ROOT/'devotionals').glob('??-??.md')):
  meta=front(f);sid=meta.get('scene')
  if sid and sid not in lib.plates:sys.exit(f'{f.name}: unknown scene {sid}')
  if not sid and meta.get('ref'):
   r=parse_ref(meta['ref'])
   if not r:continue
   key=f'file:{f.stem}:{meta["ref"]}';sid=picks.get(key)
   if sid not in lib.plates:  # devotional files are few: always picked
    sid=pick_for_verse(lib,r[0],r[1],r[2],used);picks[key]=sid;PICKS.write_text(json.dumps(picks,indent=0));print(f'{f.stem} {meta["ref"]} -> {sid}: {lib.plates[sid]["caption"]}',flush=True)
  if sid:files[f.stem]=sid;used.add(sid)
 (OUT/'days.json').write_text(json.dumps({'table':table,'files':files},separators=(',',':')))
 wanted=(set(table)|set(files.values()))-{''}
 for f in sorted((ROOT/'characters').glob('*.md')):
  sid=front(f).get('scene')
  if sid and sid in lib.plates:wanted.add(sid)
 library={sid:lib.entry(sid) for sid in sorted(wanted)}
 (OUT/'library.json').write_text(json.dumps(library,indent=1))
 print(f'days: {sum(1 for t in table if t)}/{len(table)} table + {len(files)} files; library: {len(library)} plates')
if __name__=='__main__':main()
