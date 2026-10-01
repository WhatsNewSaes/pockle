#!/usr/bin/env python3
"""Build the on-device Bible: one plain-text file per chapter of the Berean
Standard Bible (public domain), ready to pack into the LittleFS image.

  build_bible.py                 # downloads bsb.txt once, writes .tools/bible/fs/bible/<book>/<chapter>.txt
  build_bible.py --fixture       # also writes tests/fixtures/john3.txt for the host tests

Each chapter file holds its verses one per line (verse 1 is line 1) in ASCII:
the six typographic characters the BSB uses (curly quotes, em dash, ellipsis)
are mapped to their plain equivalents because the device fonts cover 32-126.
The book order and chapter counts must match `bibleBooks` in Bible.h, which is
checked here, along with every verse-of-the-day reference in that table.
"""
import argparse, pathlib, re, sys, urllib.request
ROOT=pathlib.Path(__file__).resolve().parent.parent
SRC=ROOT/'.tools/bible/bsb.txt';OUT=ROOT/'.tools/bible/fs/bible';URL='https://bereanbible.com/bsb.txt'
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
def main():
 p=argparse.ArgumentParser();p.add_argument('--fixture',action='store_true');a=p.parse_args()
 download();order,books=parse()
 fw=firmwareBooks();assert len(fw)==66,f'Bible.h lists {len(fw)} books'
 for i,(name,chapters) in enumerate(fw):
  assert order[i]==name,f'book {i+1}: firmware {name!r} vs text {order[i]!r}'
  assert len(books[name])==chapters,f'{name}: firmware {chapters} chapters vs text {len(books[name])}'
 for b,c,v in votd():
  assert 1<=b<=66 and c in books[fw[b-1][0]] and v in books[fw[b-1][0]][c],f'verse of the day {b} {c}:{v} does not exist'
 total=0;files=0
 for i,(name,chapters) in enumerate(fw,1):
  d=OUT/f'{i:02d}';d.mkdir(parents=True,exist_ok=True)
  for c in range(1,chapters+1):
   verses=books[name][c];n=max(verses)
   text='\n'.join(verses.get(v,'') for v in range(1,n+1))+'\n'
   (d/f'{c:03d}.txt').write_text(text);total+=len(text);files+=1
 print(f'wrote {files} chapter files, {total/1048576:.2f} MB, to {OUT}')
 if a.fixture:
  j=books['John'][3];(ROOT/'tests/fixtures/john3.txt').write_text('\n'.join(j[v] for v in range(1,max(j)+1))+'\n');print('wrote tests/fixtures/john3.txt')
if __name__=='__main__':main()
