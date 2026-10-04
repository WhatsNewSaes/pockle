"""The Sweet Publishing illustration library (CC BY-SA 3.0) as hosted on Wikimedia Commons:
2,358 plates named "<Book> Chapter N-i". This module maps them to the device's books, carries
the captions tools/caption_sweet.py wrote, and asks the model to pick the best plate for a verse
or a character. Scene ids are "<code>-<chapter>-<i>", e.g. 1sa-17-1."""
import json, os, pathlib, re, time, urllib.request
from biblelib import BOOKS, verse_text, book_chapters
ROOT=pathlib.Path(__file__).resolve().parent.parent;D=ROOT/'.tools/sweet'
CODES=['gen','exo','lev','num','deu','jos','jdg','rut','1sa','2sa','1ki','2ki','1ch','2ch','ezr','neh','est','job','psa','pro','ecc','sng','isa','jer','lam','ezk','dan','hos','jol','amo','oba','jon','mic','nam','hab','zep','hag','zec','mal','mat','mrk','luk','jhn','act','rom','1co','2co','gal','eph','php','col','1th','2th','1ti','2ti','tit','phm','heb','jas','1pe','2pe','1jn','2jn','3jn','jud','rev']
SWEET_NAMES={'Genesis':'Book of Genesis','Exodus':'Book of Exodus','Leviticus':'Book of Leviticus','Numbers':'Book of Numbers','Deuteronomy':'Book of Deuteronomy','Joshua':'Book of Joshua','Judges':'Book of Judges','Ruth':'Book of Ruth',
 '1 Samuel':'First Book of Samuel','2 Samuel':'Second Book of Samuel','1 Kings':'First Book of Kings','2 Kings':'Second Book of Kings','1 Chronicles':'First Book of Chronicles','2 Chronicles':'Second Book of Chronicles','Ezra':'Book of Ezra','Nehemiah':'Book of Nehemiah','Esther':'Book of Esther','Job':'Book of Job','Psalm':'Psalms',
 'Isaiah':'Book of Isaiah','Jeremiah':'Book of Jeremiah','Ezekiel':'Book of Ezekiel','Daniel':'Book of Daniel','Jonah':'Book of Jonah','Matthew':'Gospel of Matthew','Mark':'Gospel of Mark','Luke':'Gospel of Luke','John':'Gospel of John','Acts':'Acts of the Apostles',
 '1 Timothy':'First Epistle to Timothy','2 Timothy':'Second Epistle to Timothy','Titus':'Epistle to Titus','James':'Epistle of James','1 John':'First Epistle of John','Revelation':'Book of Revelation'}
NAME_TO_BOOK={name:i for i,(name,_) in enumerate(BOOKS,1)};SWEET_TO_BOOK={v:NAME_TO_BOOK[k] for k,v in SWEET_NAMES.items()}
CREDIT='Sweet Publishing, CC BY-SA 3.0'
class Library:
 def __init__(self):
  self.files=json.load(open(D/'files.json'));self.caps=json.load(open(D/'captions.json')) if (D/'captions.json').exists() else {}
  self.plates={}  # id -> {title, book, chapter, i, caption, people}
  for t in self.files:
   m=re.match(r'File:(.+?) Chapter (\d+)-(\d+) \(Bible Illustrations by Sweet Media\)\.jpg$',t)
   if not m or m.group(1) not in SWEET_TO_BOOK:continue
   b=SWEET_TO_BOOK[m.group(1)];ch,i=int(m.group(2)),int(m.group(3));sid=f'{CODES[b-1]}-{ch}-{i}';c=self.caps.get(t,{})
   self.plates[sid]={'title':t,'book':b,'chapter':ch,'i':i,'caption':c.get('caption',''),'people':c.get('people',[])}
  self.by_chapter={}
  for sid,p in self.plates.items():self.by_chapter.setdefault((p['book'],p['chapter']),[]).append(sid)
  for v in self.by_chapter.values():v.sort(key=lambda s:self.plates[s]['i'])
 def chapter(self,book,ch):return list(self.by_chapter.get((book,ch),[]))
 def book(self,book,chapters=None):
  return [s for (b,c),ids in sorted(self.by_chapter.items()) if b==book and (chapters is None or c in chapters) for s in ids]
 def passage(self,sid):p=self.plates[sid];return f'{BOOKS[p["book"]-1][0]} {p["chapter"]}'
 def entry(self,sid):p=self.plates[sid];return {'commons':p['title'],'passage':self.passage(sid),'caption':p['caption'],'people':p['people'],'credit':CREDIT}
# Where to look for a verse whose own chapter has no plate: the stories a book's verses sit beside.
R=lambda a,b:set(range(a,b+1))
POOL={19:[(19,None),(9,R(16,31)),(10,None)],20:[(11,R(1,11))],21:[(11,R(1,11))],22:[(11,R(1,11))],
 23:[(23,None),(12,R(18,20)),(14,None)],24:[(24,None),(12,R(22,25))],25:[(24,None),(12,R(22,25))],26:[(26,None)],
 3:[(2,None),(3,None),(4,None)],4:[(4,None),(2,None)],5:[(5,None),(4,None),(2,None)],13:[(13,None),(10,None),(11,None)],14:[(14,None),(11,None),(12,None)],
 58:[(1,None),(2,None),(3,None),(6,None),(7,None)],59:[(59,None),(44,R(15,15))],60:[(40,{14,16,26}),(43,{21}),(44,R(1,12))],61:[(40,{14,16,26}),(43,{21}),(44,R(1,12))],
 62:[(62,None),(43,None)],63:[(62,None),(43,None)],64:[(62,None),(43,None)],65:[(44,None)],66:[(66,None)]}
for b in range(45,58):POOL[b]=[(44,R(9,28)),(54,None),(55,None),(56,None)]  # Paul's letters: his journeys and his letters to Timothy and Titus
for b in list(range(28,32))+list(range(33,40)):POOL[b]=[(11,R(12,22)),(12,R(1,17)),(16,None),(15,None)]  # the minor prophets: the kings they spoke to, and the return
def candidates(lib,book,chapter,used=set(),want=4,cap=160):
 ids=[s for s in lib.chapter(book,chapter) if s not in used]
 if len(ids)<want:
  for d in (1,2):
   for ch in (chapter-d,chapter+d):ids+=[s for s in lib.chapter(book,ch) if s not in used and s not in ids]
 if len(ids)<want:
  for b,chs in POOL.get(book,[(book,None)]):ids+=[s for s in lib.book(b,chs) if s not in used and s not in ids]
 if len(ids)<want:ids+=[s for s in lib.book(42) if s not in used and s not in ids]  # Luke: the fullest set of Jesus plates
 if len(ids)>cap:step=len(ids)/cap;ids=[ids[int(k*step)] for k in range(cap)]
 return ids
_KEY=None
def model(messages,max_tokens=600,temperature=0.2):
 global _KEY
 if _KEY is None:_KEY=re.search(r'OPENROUTER_API_KEY=(\S+)',pathlib.Path(os.path.expanduser('~/.hermes/.env')).read_text()).group(1)
 body={'model':'google/gemini-2.5-flash','response_format':{'type':'json_object'},'max_tokens':max_tokens,'temperature':temperature,'messages':messages}
 for a in range(4):
  try:
   r=json.load(urllib.request.urlopen(urllib.request.Request('https://openrouter.ai/api/v1/chat/completions',data=json.dumps(body).encode(),headers={'Authorization':'Bearer '+_KEY,'Content-Type':'application/json'}),timeout=120))
   txt=r['choices'][0]['message']['content'];return json.loads(txt[txt.index('{'):txt.rindex('}')+1])
  except Exception as e:err=e;time.sleep(3+a*5)
 raise RuntimeError(f'model failed: {err}')
def listing(lib,ids):return '\n'.join(f'{s} | {lib.plates[s]["caption"] or lib.passage(s)}' for s in ids)
def pick_for_verse(lib,book,chapter,verse,used):
 ids=candidates(lib,book,chapter,used);ref=f'{BOOKS[book-1][0]} {chapter}:{verse}';text=verse_text(book,chapter,verse) or ''
 if len(ids)==1:return ids[0]
 j=model([{'role':'user','content':f'A child\'s daily devotional for the verse {ref} - "{text}" needs one picture. Choose the illustration from this list that fits the verse best: a scene from the verse\'s own story if one is listed, otherwise the closest in theme (same person, same kind of moment). '
  f'Reply with strict JSON {{"id": <the id exactly as listed>}}.\n\n{listing(lib,ids)}'}],max_tokens=200,temperature=0)
 sid=str(j.get('id','')).strip();return sid if sid in ids else ids[0]
