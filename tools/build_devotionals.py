#!/usr/bin/env python3
"""Devotionals as Markdown, one file per day, exported to the JSON the device fetches.

  devotionals/10-01.md  ->  devotionals/out/10-01.json

  build_devotionals.py            # validate every devotionals/*.md and write devotionals/out/
  build_devotionals.py --draft 7  # also draft the next 7 days that have no file yet (uses the model)
  build_devotionals.py --draft all

A file looks like:

    ---
    ref: Psalm 119:9            # optional; becomes that day's verse of the day on the launcher
    title: Clean Heart, Clean Path
    ---
    ## Truth
    One sentence.

    - Three short bullets.
    - ...
    - ...

    ## Do it today
    One or two short sentences.

    ## Pray
    One sentence.

Limits keep it to one screen: title 40 characters, truth 140, each bullet 90, apply 200, prayer 160.
The device fetches devotionals/out/MM-DD.json from the repo's main branch each day; a day with no
file falls back to a devotional the model writes on the device.
"""
import argparse, datetime, json, os, pathlib, re, sys, urllib.request
ROOT=pathlib.Path(__file__).resolve().parent.parent
SRC=ROOT/'devotionals';OUT=SRC/'out'
LIMITS={'title':40,'truth':140,'point':90,'apply':200,'prayer':160}
BIBLE=ROOT/'.tools/bible/fs/bible'
def books():
 src=(ROOT/'firmware/RetroSports/Bible.h').read_text()
 table=re.search(r'bibleBooks\[\]\s*=\s*\{(.*?)\};',src,re.S).group(1)
 return [(m.group(1),int(m.group(2))) for m in re.finditer(r'\{"([^"]+)","[^"]*",(\d+)\}',table)]
def votd_table():
 src=(ROOT/'firmware/RetroSports/Bible.h').read_text()
 table=re.search(r'verseOfDayTable\[\]\s*=\s*\{(.*?)\};',src,re.S).group(1)
 return [(int(m.group(1)),int(m.group(2)),int(m.group(3))) for m in re.finditer(r'\{(\d+),(\d+),(\d+)\}',table)]
BOOKS=books()
def parse_ref(text):
 m=re.match(r'^\s*(.+?)\s+(\d+):(\d+)\s*$',text)
 if not m:return None
 name=m.group(1).lower().replace('.','').strip();chapter,verse=int(m.group(2)),int(m.group(3))
 for i,(b,chapters) in enumerate(BOOKS,1):
  if b.lower()==name or (name in('psalms',) and b=='Psalm') or (name=='song of songs' and b=='Song of Solomon'):
   if 1<=chapter<=chapters:return i,chapter,verse
 return None
def verse_text(book,chapter,verse):
 f=BIBLE/f'{book:02d}'/f'{chapter:03d}.txt'
 if not f.exists():return None
 lines=f.read_text().split('\n')
 return lines[verse-1] if 1<=verse<=len(lines) and lines[verse-1] else None
def parse_md(path):
 text=path.read_text()
 m=re.match(r'^---\n(.*?)\n---\n(.*)$',text,re.S)
 if not m:raise ValueError(f'{path.name}: missing front matter')
 meta={};body=m.group(2)
 for line in m.group(1).split('\n'):
  if ':' in line:k,v=line.split(':',1);meta[k.strip()]=v.split('#')[0].strip() if k.strip()!='ref' else v.split('#')[0].strip()
 sections={};cur=None
 for line in body.split('\n'):
  h=re.match(r'^##\s+(.+?)\s*$',line)
  if h:cur=h.group(1).lower();sections[cur]=[];continue
  if cur is not None and line.strip():sections[cur].append(line.rstrip())
 def para(key):
  lines=[l for l in sections.get(key,[]) if not l.lstrip().startswith('-')]
  return ' '.join(l.strip() for l in lines)
 points=[l.lstrip()[1:].strip() for l in sections.get('truth',[]) if l.lstrip().startswith('-')]
 d={'title':meta.get('title','').strip(),'truth':para('truth'),'points':points,'apply':para('do it today'),'prayer':para('pray')}
 if meta.get('ref'):d['ref']=meta['ref']
 return d
def validate(name,d):
 errs=[]
 for k in('title','truth','apply','prayer'):
  if not d.get(k):errs.append(f'{k} is empty')
  elif len(d[k])>LIMITS[k]:errs.append(f'{k} is {len(d[k])} characters (max {LIMITS[k]})')
 if len(d.get('points',[]))!=3:errs.append(f'{len(d.get("points",[]))} bullets (need 3)')
 for p in d.get('points',[]):
  if len(p)>LIMITS['point']:errs.append(f'bullet "{p[:30]}..." is {len(p)} characters (max {LIMITS["point"]})')
 if d.get('ref') and not parse_ref(d['ref']):errs.append(f'ref "{d["ref"]}" is not a verse the device knows')
 for k,v in list(d.items()):
  if isinstance(v,str) and any(ord(c)>126 for c in v):d[k]=v.replace('’',"'").replace('‘',"'").replace('“','"').replace('”','"').replace('—',' - ').replace('…','...')
 d['points']=[p.replace('’',"'").replace('“','"').replace('”','"') for p in d['points']]
 return errs
def day_verse(month,day):
 yday=datetime.date(2024,month,day).timetuple().tm_yday-1  # leap-year calendar so 02-29 exists
 t=votd_table();b,c,v=t[yday%len(t)]
 return b,c,v
def draft(month,day):
 key=re.search(r'OPENROUTER_API_KEY=(\S+)',pathlib.Path(os.path.expanduser('~/.hermes/.env')).read_text()).group(1)
 b,c,v=day_verse(month,day);ref=f'{BOOKS[b-1][0]} {c}:{v}';text=verse_text(b,c,v)
 chapter=(BIBLE/f'{b:02d}'/f'{c:03d}.txt').read_text().split('\n')
 ctx=' '.join(f'{i} {chapter[i-1]}' for i in range(max(1,v-6),min(len(chapter),v+6)+1) if chapter[i-1])
 src=(ROOT/'firmware/RetroSports/Devotional.h').read_text()
 body=src[src.index('devotionalSystemPrompt(){'):src.index('inline std::string devotionalRequestBody')]
 system=''.join(re.findall(r'"((?:[^"\\]|\\.)*)"',body)).replace('\\"','"')
 req=urllib.request.Request('https://openrouter.ai/api/v1/chat/completions',data=json.dumps({'model':'google/gemini-2.5-flash','response_format':{'type':'json_object'},'max_tokens':1500,'temperature':0.7,'messages':[{'role':'system','content':system},{'role':'user','content':f'Verse of the day: {ref} - {text}\nContext: {ctx}'}]}).encode(),headers={'Authorization':'Bearer '+key,'Content-Type':'application/json'})
 d=json.loads(json.load(urllib.request.urlopen(req,timeout=120))['choices'][0]['message']['content'])
 md=f"---\nref: {ref}\ntitle: {d['title']}\n---\n## Truth\n{d['truth']}\n\n"+''.join(f"- {p}\n" for p in d['points'])+f"\n## Do it today\n{d['apply']}\n\n## Pray\n{d['prayer']}\n"
 return md
def main():
 p=argparse.ArgumentParser();p.add_argument('--draft',help='N days ahead, or "all"');a=p.parse_args()
 SRC.mkdir(exist_ok=True);OUT.mkdir(exist_ok=True)
 if a.draft:
  today=datetime.date.today();n=366 if a.draft=='all' else int(a.draft);made=0
  for i in range(n):
   day=today+datetime.timedelta(days=i);f=SRC/f'{day.month:02d}-{day.day:02d}.md'
   if f.exists():continue
   f.write_text(draft(day.month,day.day));made+=1;print('drafted',f.name)
  print(f'{made} drafts written; edit them, then run again to export')
 bad=0;count=0
 for f in sorted(SRC.glob('*.md')):
  if not re.match(r'^\d\d-\d\d\.md$',f.name):print(f'skip {f.name}: name must be MM-DD.md');continue
  try:d=parse_md(f)
  except ValueError as e:print(e);bad+=1;continue
  errs=validate(f.name,d)
  if errs:bad+=1;print(f'{f.name}:');[print('  -',e) for e in errs];continue
  (OUT/f.name.replace('.md','.json')).write_text(json.dumps(d,ensure_ascii=True,indent=1));count+=1
 print(f'exported {count} devotional(s) to {OUT.relative_to(ROOT)}'+(f', {bad} with problems' if bad else ''))
 sys.exit(1 if bad else 0)
if __name__=='__main__':main()
