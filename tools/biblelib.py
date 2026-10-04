"""Read the packed on-device Bible (.tools/bible/fs/bible/<code>/NN.z) from the build tools."""
import pathlib, re, struct, zlib
ROOT=pathlib.Path(__file__).resolve().parent.parent
BIBLE=ROOT/'.tools/bible/fs/bible'
def books():
 src=(ROOT/'firmware/RetroSports/Bible.h').read_text()
 table=re.search(r'bibleBooks\[\]\s*=\s*\{(.*?)\};',src,re.S).group(1)
 return [(m.group(1),int(m.group(2))) for m in re.finditer(r'\{"([^"]+)","[^"]*",(\d+)\}',table)]
BOOKS=books()
_cache={}
def book_chapters(book,code='bsb'):
 """Chapters of book number `book` (1-66) as lists of verse strings (verse 1 at index 0)."""
 key=(code,book)
 if key not in _cache:
  f=BIBLE/code/f'{book:02d}.z'
  if not f.exists():return []
  raw=f.read_bytes();n=struct.unpack('<I',raw[:4])[0];text=zlib.decompress(raw[4:]).decode()
  _cache[key]=[c.rstrip('\n').split('\n') for c in text.split('\x1e')]
 return _cache[key]
def verse_text(book,chapter,verse,code='bsb'):
 ch=book_chapters(book,code)
 if not 1<=chapter<=len(ch):return None
 vs=ch[chapter-1];return vs[verse-1] if 1<=verse<=len(vs) and vs[verse-1] else None
def parse_ref(text):
 m=re.match(r'^\s*(.+?)\s+(\d+)(?::(\d+))?\s*$',text)
 if not m:return None
 name=m.group(1).lower().replace('.','').strip();chapter=int(m.group(2));verse=int(m.group(3) or 0)
 for i,(b,chapters) in enumerate(BOOKS,1):
  if b.lower()==name or (name=='psalms' and b=='Psalm') or (name=='song of songs' and b=='Song of Solomon'):
   if 1<=chapter<=chapters:return i,chapter,verse
 return None
