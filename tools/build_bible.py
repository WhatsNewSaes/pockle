#!/usr/bin/env python3
"""Build the on-device Bible: one plain-text file per chapter of the Berean
Standard Bible (public domain), ready to pack into the LittleFS image.

  build_bible.py                 # downloads the sources once, writes .tools/bible/fs/bible/<code>/NN.z for every translation
  build_bible.py --only bsb,fbv  # a subset
  build_bible.py --fixture       # also writes tests/fixtures/john3.txt for the host tests

Each chapter file holds its verses one per line (verse 1 is line 1) in ASCII:
the six typographic characters the BSB uses (curly quotes, em dash, ellipsis)
are mapped to their plain equivalents because the device fonts cover 32-126.
The book order and chapter counts must match `bibleBooks` in Bible.h, which is
checked here, along with every verse-of-the-day reference in that table.
"""
import argparse, json, pathlib, re, struct, sys, urllib.request, zlib
ROOT=pathlib.Path(__file__).resolve().parent.parent
SRC=ROOT/'.tools/bible/bsb.txt';OUT=ROOT/'.tools/bible/fs/bible/bsb';URL='https://bereanbible.com/bsb.txt'
# One zlib file per book: a 4-byte little-endian raw length, then the deflated text with chapters
# separated by \x1e (verses one per line). 66 files cost ~1.35 MB on LittleFS instead of 6.3 MB for 1,189.
CHAPTER_SEP='\x1e'
ASCII={'“':'"','”':'"','‘':"'",'’':"'",'—':' - ','…':'...',' ':' '}
def download():
 if SRC.exists():return
 SRC.parent.mkdir(parents=True,exist_ok=True);print('downloading',URL);urllib.request.urlretrieve(URL,SRC)
def parse():
 books={};order=[]
 for line in SRC.read_text(encoding='utf-8-sig').split('\n'):
  if '\t' not in line:continue
  ref,text=line.split('\t',1);m=re.match(r'(.+) (\d+):(\d+)$',ref)
  if not m:continue
  book,chapter,verse=m.group(1),int(m.group(2)),int(m.group(3))
  if book not in books:books[book]={};order.append(book)
  for k,v in ASCII.items():text=text.replace(k,v)
  text=re.sub(r'\s+',' ',text).strip()
  if any(ord(c)>126 for c in text):sys.exit(f'non-ASCII left in {ref}: {text!r}')
  books[book].setdefault(chapter,{})[verse]=text
 return order,books
def firmwareBooks():
 src=(ROOT/'firmware/RetroSports/Bible.h').read_text()
 table=re.search(r'bibleBooks\[\]\s*=\s*\{(.*?)\};',src,re.S).group(1)
 return [(m.group(1),int(m.group(2))) for m in re.finditer(r'\{"([^"]+)","[^"]*",(\d+)\}',table)]
def votd():
 src=(ROOT/'firmware/RetroSports/Bible.h').read_text()
 table=re.search(r'verseOfDayTable\[\]\s*=\s*\{(.*?)\};',src,re.S).group(1)
 return [(int(m.group(1)),int(m.group(2)),int(m.group(3))) for m in re.finditer(r'\{(\d+),(\d+),(\d+)\}',table)]
# Every translation the device can carry: where it comes from, how it is credited, and a one-line
# description parents can read in the picker. All use the standard Protestant verse numbering.
TRANSLATIONS={
 'bsb':{'name':'Berean Standard Bible','short':'BSB','source':'bsb','license':'Public domain (Bible Hub, 2023)','closest':'ESV, NASB, CSB',
  'blurb':'Modern, clear English that reads like a typical church Bible - accurate and easy to follow.'},
 'fbv':{'name':'Free Bible Version','short':'FBV','source':'ebible:engfbv','license':'(c) 2018 Jonathan Gallagher, CC BY-SA 4.0','closest':'NLT, NIV',
  'blurb':'Plain, everyday English written for readability - the easiest to understand for new readers.'},
 'bbe':{'name':'Bible in Basic English','short':'BBE','source':'ebible:engBBE','license':'Public domain (1949/1965)','closest':'NIrV, ERV',
  'blurb':'Uses only about 1,000 simple words, so young children can read it on their own.'},
 'kjv':{'name':'King James Version','short':'KJV','source':'ebible:eng-kjv','license':'Public domain (1611, 1769 text)',
  'blurb':"The classic 1611 English Bible with 'thee' and 'thou' - the wording many hymns and memory verses use."},
}
# eBible.org verse-per-line files use a few older book codes.
EBIBLE_CODES=['GEN','EXO','LEV','NUM','DEU','JOS','JDG','RUT','1SA','2SA','1KI','2KI','1CH','2CH','EZR','NEH','EST','JOB','PSA','PRO','ECC','SNG','ISA','JER','LAM','EZK','DAN','HOS','JOL','AMO','OBA','JON','MIC','NAM','HAB','ZEP','HAG','ZEC','MAL','MAT','MRK','LUK','JHN','ACT','ROM','1CO','2CO','GAL','EPH','PHP','COL','1TH','2TH','1TI','2TI','TIT','PHM','HEB','JAS','1PE','2PE','1JN','2JN','3JN','JUD','REV']
EBIBLE_ALIASES={'SON':'SNG','SOL':'SNG','EZE':'EZK','JOE':'JOL','NAH':'NAM','MAR':'MRK','JOH':'JHN','PHI':'PHP','JAM':'JAS','1JO':'1JN','2JO':'2JN','3JO':'3JN'}
MORE_ASCII={'\u00b6':'','\u00e6':'ae','\u00c6':'Ae','\u00e9':'e','\u00e8':'e','\u00ea':'e','\u00e1':'a','\u00f1':'n','\u00a0':' ','\u2013':'-','\u00ab':'"','\u00bb':'"'}
def clean(text):
 for k,v in {**ASCII,**MORE_ASCII}.items():text=text.replace(k,v)
 text=re.sub(r'\s+',' ',text).strip()
 if any(ord(c)>126 for c in text):sys.exit(f'non-ASCII left: {text!r}')
 return text
def parse_ebible(tid,order):
 import io,zipfile
 z=ROOT/f'.tools/bible/{tid}_vpl.zip'
 if not z.exists():print('downloading',tid);req=urllib.request.Request(f'https://ebible.org/Scriptures/{tid}_vpl.zip',headers={'User-Agent':'Mozilla/5.0 (pockle build)'});z.write_bytes(urllib.request.urlopen(req,timeout=120).read()) # eBible refuses the default Python agent
 txt=zipfile.ZipFile(z).read(f'{tid}_vpl.txt').decode('utf-8')
 books={name:{} for name in order};bycode=dict(zip(EBIBLE_CODES,order))
 for line in txt.split('\n'):
  m=re.match(r'^(\S+) (\d+):(\d+) (.*)$',line)
  if not m:continue
  code=EBIBLE_ALIASES.get(m.group(1),m.group(1))
  if code not in bycode:continue
  books[bycode[code]].setdefault(int(m.group(2)),{})[int(m.group(3))]=clean(m.group(4))
 return books
def write_translation(code,books,fw):
 out=ROOT/'.tools/bible/fs/bible'/code;out.mkdir(parents=True,exist_ok=True);total=0;packed=0
 for i,(name,chapters) in enumerate(fw,1):
  parts=[]
  for c in range(1,chapters+1):
   verses=books[name].get(c,{});n=max(verses) if verses else 0
   parts.append('\n'.join(verses.get(v,'') for v in range(1,n+1))+'\n')
  raw=CHAPTER_SEP.join(parts).encode();z=zlib.compress(raw,9)
  (out/f'{i:02d}.z').write_bytes(struct.pack('<I',len(raw))+z);total+=len(raw);packed+=len(z)+4
 t=TRANSLATIONS[code];(out/'meta.json').write_text(json.dumps({'code':code,'name':t['name'],'short':t['short'],'license':t['license'],'blurb':t['blurb'],'closest':t.get('closest','')}))
 print(f'{code}: 66 books, {total/1048576:.2f} MB of text packed to {packed/1048576:.2f} MB')
def main():
 p=argparse.ArgumentParser();p.add_argument('--fixture',action='store_true');p.add_argument('--only',help='comma-separated translation codes');a=p.parse_args()
 download();order,bsb=parse()
 fw=firmwareBooks();assert len(fw)==66,f'Bible.h lists {len(fw)} books'
 for i,(name,chapters) in enumerate(fw):
  assert order[i]==name,f'book {i+1}: firmware {name!r} vs text {order[i]!r}'
  assert len(bsb[name])==chapters,f'{name}: firmware {chapters} chapters vs text {len(bsb[name])}'
 for b,c,v in votd():
  assert 1<=b<=66 and c in bsb[fw[b-1][0]] and v in bsb[fw[b-1][0]][c],f'verse of the day {b} {c}:{v} does not exist'
 base=ROOT/'.tools/bible/fs/bible'
 for old in base.glob('[0-9][0-9]'):  # the previous one-file-per-chapter layout
  for f in old.glob('*.txt'):f.unlink()
  old.rmdir()
 wanted=a.only.split(',') if a.only else list(TRANSLATIONS)
 for code in wanted:
  t=TRANSLATIONS[code]
  books=bsb if t['source']=='bsb' else parse_ebible(t['source'].split(':')[1],order)
  for name,chapters in fw:
   got=len([c for c in books[name] if books[name][c]]);assert got==chapters,f'{code} {name}: {got} chapters, expected {chapters}'
  write_translation(code,books,fw)
 index={code:{'name':t['name'],'short':t['short'],'license':t['license'],'blurb':t['blurb'],'closest':t.get('closest','')} for code,t in TRANSLATIONS.items() if (base/code/'66.z').exists()}
 (base/'index.json').write_text(json.dumps(index,separators=(',',':')))
 print('index:',', '.join(index))
 if a.fixture:
  j=bsb['John'][3];(ROOT/'tests/fixtures/john3.txt').write_text('\n'.join(j[v] for v in range(1,max(j)+1))+'\n');print('wrote tests/fixtures/john3.txt')
if __name__=='__main__':main()
