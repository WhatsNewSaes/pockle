#!/usr/bin/env python3
"""Draft the Bible character cards: characters/<id>.md for everyone in ROSTER who has no file yet.

  draft_characters.py            # draft the missing cards (uses the model; picks each card's plate from the Sweet library)
  draft_characters.py --redo david

For each person the model chooses the plate that shows them best (from the chapters listed here) and
writes the blurb and bullets; the passages come from this table. Edit the .md afterwards as you like:
`scene:` is the plate, `chapters:` the plates it may pick from, `order:` the place in the list.
"""
import argparse, json, pathlib, re, sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from sweet import Library, model, listing
from biblelib import parse_ref
ROOT=pathlib.Path(__file__).resolve().parent.parent;SRC=ROOT/'characters'
# name, chapters whose plates may show them, passages to read (first is the main story)
ROSTER=[
 ('Adam and Eve','Genesis 2, Genesis 3','Genesis 2, Genesis 3'),('Cain and Abel','Genesis 4','Genesis 4'),('Noah','Genesis 6, Genesis 7, Genesis 8, Genesis 9','Genesis 6, Genesis 7, Genesis 8'),
 ('Abraham','Genesis 12, Genesis 15, Genesis 18, Genesis 22','Genesis 12, Genesis 15, Genesis 22'),('Sarah','Genesis 18, Genesis 21','Genesis 18, Genesis 21'),('Lot','Genesis 13, Genesis 19','Genesis 13, Genesis 19'),
 ('Isaac','Genesis 22, Genesis 24, Genesis 26','Genesis 22, Genesis 26'),('Rebekah','Genesis 24','Genesis 24'),('Jacob','Genesis 28, Genesis 32, Genesis 33','Genesis 28, Genesis 32'),('Esau','Genesis 25, Genesis 27, Genesis 33','Genesis 25, Genesis 33'),
 ('Joseph','Genesis 37, Genesis 39, Genesis 41, Genesis 45','Genesis 37, Genesis 41, Genesis 45'),('Moses','Exodus 2, Exodus 3, Exodus 14, Exodus 20','Exodus 3, Exodus 14, Exodus 20'),('Aaron','Exodus 4, Exodus 7, Exodus 32','Exodus 4, Exodus 32'),
 ('Miriam','Exodus 2, Exodus 15','Exodus 2, Exodus 15'),('Balaam','Numbers 22','Numbers 22'),('Joshua','Joshua 1, Joshua 3, Joshua 6','Joshua 1, Joshua 6'),('Caleb','Numbers 13, Numbers 14, Joshua 14','Numbers 13, Joshua 14'),
 ('Rahab','Joshua 2, Joshua 6','Joshua 2'),('Deborah','Judges 4, Judges 5','Judges 4'),('Gideon','Judges 6, Judges 7','Judges 6, Judges 7'),('Samson','Judges 13, Judges 14, Judges 15, Judges 16','Judges 13, Judges 16'),
 ('Ruth','Ruth 1, Ruth 2, Ruth 3, Ruth 4','Ruth 1, Ruth 2'),('Naomi','Ruth 1, Ruth 4','Ruth 1, Ruth 4'),('Boaz','Ruth 2, Ruth 3, Ruth 4','Ruth 2, Ruth 4'),('Hannah','1 Samuel 1, 1 Samuel 2','1 Samuel 1'),
 ('Samuel','1 Samuel 3, 1 Samuel 7, 1 Samuel 16','1 Samuel 3, 1 Samuel 16'),('Saul','1 Samuel 9, 1 Samuel 10, 1 Samuel 15','1 Samuel 9, 1 Samuel 15'),('David','1 Samuel 16, 1 Samuel 17','1 Samuel 17, Psalm 23, 2 Samuel 7'),
 ('Goliath','1 Samuel 17','1 Samuel 17'),('Jonathan','1 Samuel 18, 1 Samuel 20','1 Samuel 18, 1 Samuel 20'),('Nathan','2 Samuel 12','2 Samuel 12'),('Mephibosheth','2 Samuel 9','2 Samuel 9'),
 ('Absalom','2 Samuel 15, 2 Samuel 18','2 Samuel 15, 2 Samuel 18'),('Solomon','1 Kings 3, 1 Kings 8, 1 Kings 10','1 Kings 3, Proverbs 3'),('Elijah','1 Kings 17, 1 Kings 18, 1 Kings 19','1 Kings 17, 1 Kings 18'),
 ('Elisha','2 Kings 2, 2 Kings 4, 2 Kings 6','2 Kings 2, 2 Kings 4'),('Naaman','2 Kings 5','2 Kings 5'),('Hezekiah','2 Kings 18, 2 Kings 19, 2 Kings 20','2 Kings 19, Isaiah 37'),('Josiah','2 Kings 22, 2 Kings 23','2 Kings 22, 2 Kings 23'),
 ('Isaiah','Isaiah 6, 2 Kings 19, 2 Kings 20','Isaiah 6, Isaiah 53'),('Jeremiah','Jeremiah 1, Jeremiah 36, Jeremiah 38','Jeremiah 1, Jeremiah 29'),('Ezekiel','Ezekiel 1, Ezekiel 37','Ezekiel 37'),
 ('Daniel','Daniel 1, Daniel 2, Daniel 6','Daniel 1, Daniel 6'),('Shadrach, Meshach and Abednego','Daniel 3','Daniel 3'),('Nebuchadnezzar','Daniel 2, Daniel 3, Daniel 4','Daniel 4'),('Jonah','Jonah 1, Jonah 2, Jonah 3','Jonah 1, Jonah 2, Jonah 3'),
 ('Esther','Esther 2, Esther 4, Esther 5, Esther 7','Esther 4, Esther 7'),('Mordecai','Esther 2, Esther 6','Esther 2, Esther 6'),('Haman','Esther 3, Esther 7','Esther 3, Esther 7'),('Ezra','Ezra 7, Ezra 8, Ezra 10','Ezra 7'),
 ('Nehemiah','Nehemiah 2, Nehemiah 4, Nehemiah 6','Nehemiah 2, Nehemiah 4'),('Job','Job 1, Job 2, Job 42','Job 1, Job 42'),
 ('Mary, the Mother of Jesus','Luke 1, Luke 2','Luke 1, Luke 2'),('Joseph of Nazareth','Matthew 1, Matthew 2, Luke 2','Matthew 1, Matthew 2'),('Elizabeth','Luke 1','Luke 1'),('John the Baptist','Matthew 3, Mark 1, Luke 3','Matthew 3, Luke 3'),
 ('Jesus','Luke 4, Luke 5, Matthew 5, Matthew 8, John 1','Luke 2, John 3, Matthew 28'),('Peter','Matthew 14, Matthew 16, Luke 5, John 21, Acts 2','Luke 5, Matthew 14, Acts 2'),('Andrew','John 1, John 6, Mark 1','John 1, John 6'),
 ('James','Matthew 4, Matthew 17, Mark 10','Matthew 4, Mark 10'),('John','John 13, John 19, John 21, Mark 1','John 13, John 21'),('Matthew','Luke 5','Matthew 9, Luke 5'),('Thomas','John 20','John 20'),
 ('Judas Iscariot','Matthew 26, Luke 22','Matthew 26'),('Mary Magdalene','John 20, Luke 8','John 20'),('Martha','Luke 10, John 11','Luke 10, John 11'),('Mary of Bethany','Luke 10, John 11, John 12','Luke 10, John 12'),
 ('Lazarus','John 11','John 11'),('Zacchaeus','Luke 19','Luke 19'),('Nicodemus','John 3','John 3'),('Bartimaeus','Mark 10','Mark 10'),('Pontius Pilate','Matthew 27, John 18, John 19','Matthew 27, John 18'),
 ('Stephen','Acts 6, Acts 7','Acts 6, Acts 7'),('Philip','Acts 8','Acts 8'),('The Ethiopian Official','Acts 8','Acts 8'),('Dorcas','Acts 9','Acts 9'),('Cornelius','Acts 10','Acts 10'),
 ('Paul','Acts 9, Acts 16, Acts 27, Acts 28','Acts 9, Acts 16, Philippians 4'),('Barnabas','Acts 11, Acts 13, Acts 14','Acts 11, Acts 13'),('Silas','Acts 16','Acts 16'),('Timothy','Acts 16, 1 Timothy 4, 2 Timothy 1','Acts 16, 2 Timothy 1'),
 ('Lydia','Acts 16','Acts 16'),('Priscilla and Aquila','Acts 18','Acts 18'),
]
def slug(name):return re.sub(r'[^a-z0-9]+','-',name.lower().replace(' and ',' ')).strip('-')[:26]  # LittleFS file names are short
def draft(lib,name,chapters,passages):
 ids=[]
 for ch in chapters.split(','):
  r=parse_ref(ch.strip())
  if not r:sys.exit(f'{name}: bad chapter {ch}')
  ids+=lib.chapter(r[0],r[1])
 if not ids:  # the listed chapters have no plates: anywhere in those books
  for ch in chapters.split(','):
   r=parse_ref(ch.strip());ids+=[s for s in lib.book(r[0]) if s not in ids]
 if not ids:print(f'{name}: no plates in {chapters}, skipped');return None
 j=model([{'role':'user','content':f'You are writing a Bible character card for kids aged 7 to 13 on a small e-paper screen. Character: {name}. Main passages: {passages}.\n'
  f'1. Pick the illustration from the list below that shows {name} best as the main figure (the id exactly as listed).\n'
  f'2. Write "blurb": two or three short sentences (at most 380 characters in total) telling who they were and what happened, in plain warm words a 7-year-old understands; where it fits naturally, say how their story points to Jesus or shows God\'s grace. Do not quote verses.\n'
  f'3. Write "bullets": three short facts to remember (each at most 60 characters), concrete and specific.\n'
  f'Reply with strict JSON {{"id": ..., "blurb": ..., "bullets": [...]}}.\n\nIllustrations:\n{listing(lib,ids)}'}],max_tokens=900,temperature=0.6)
 sid=str(j.get('id','')).strip()
 if sid not in ids:sid=ids[0]
 blurb=re.sub(r'\s+',' ',str(j.get('blurb',''))).strip();bullets=[re.sub(r'\s+',' ',str(b)).strip() for b in j.get('bullets',[])][:3]
 for k,v in {'’':"'",'‘':"'",'“':'"','”':'"','—':' - ','…':'...'}.items():blurb=blurb.replace(k,v);bullets=[b.replace(k,v) for b in bullets]
 return sid,blurb,bullets
def main():
 p=argparse.ArgumentParser();p.add_argument('--redo',nargs='*',default=None);a=p.parse_args();lib=Library();made=0
 for order,(name,chapters,passages) in enumerate(ROSTER,1):
  f=SRC/f'{slug(name)}.md'
  if f.exists() and not (a.redo is not None and (not a.redo or slug(name) in a.redo)):continue
  got=draft(lib,name,chapters,passages)
  if not got:continue
  sid,blurb,bullets=got
  f.write_text(f'---\nname: {name}\nscene: {sid}\nchapters: {chapters}\npassages: {passages}\norder: {order}\n---\n{blurb}\n\n'+''.join(f'- {b}\n' for b in bullets));made+=1
  print(f'{f.name}: {sid} ({lib.plates[sid]["caption"]})',flush=True)
 print(f'{made} card(s) drafted; now run build_characters.py')
if __name__=='__main__':main()
