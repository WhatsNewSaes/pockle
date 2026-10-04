#!/usr/bin/env python3
"""Caption every Sweet Publishing plate on Wikimedia Commons (their Commons descriptions only name the
chapter), so tools/pick_scenes.py can match plates to verses and characters.

  caption_sweet.py            # .tools/sweet/files.json + meta.json -> .tools/sweet/captions.json (resumable)

Uses the vision model through OpenRouter (key from ~/.hermes/.env, never stored here). Thumbnails are
cached in .tools/sweet/thumbs/. Each entry: {"caption": one sentence, "people": [names shown]}.
"""
import base64, concurrent.futures, hashlib, json, os, pathlib, re, sys, threading, time, urllib.parse, urllib.request
ROOT=pathlib.Path(__file__).resolve().parent.parent;D=ROOT/'.tools/sweet';TH=D/'thumbs';TH.mkdir(parents=True,exist_ok=True)
UA={'User-Agent':'Pockle/1.0 (e-paper devotional; https://github.com/WhatsNewSaes/pockle)'}
KEY=re.search(r'OPENROUTER_API_KEY=(\S+)',pathlib.Path(os.path.expanduser('~/.hermes/.env')).read_text()).group(1)
OUT=D/'captions.json';caps=json.loads(OUT.read_text()) if OUT.exists() else {};lock=threading.Lock()
PROMPT=('This is a Bible illustration of {ref}. In strict JSON give {{"caption": <one plain sentence, at most 18 words, saying what is happening and naming the Bible people shown when you can tell from the passage>, '
 '"people": [<names of Bible people clearly depicted, e.g. "David", "Goliath", "Jesus"; empty if none identifiable>]}}. No markdown.')
def thumb(title):
 f=TH/(hashlib.sha1(title.encode()).hexdigest()[:12]+'.jpg')
 if not f.exists():
  url='https://commons.wikimedia.org/wiki/Special:FilePath/'+urllib.parse.quote(title.replace('File:',''))+'?width=400'
  for a in range(4):
   try:f.write_bytes(urllib.request.urlopen(urllib.request.Request(url,headers=UA),timeout=60).read());break
   except Exception as e:time.sleep(2+a*3)
 return f.read_bytes() if f.exists() else b''
def ask(title):
 m=re.match(r'File:(.+?) Chapter (\d+)-\d+',title);ref=f'{m.group(1)} chapter {m.group(2)}' if m else title
 raw=thumb(title)
 if not raw:return {'caption':'','people':[],'error':'no thumbnail'}
 img=base64.b64encode(raw).decode()
 body={'model':'google/gemini-2.5-flash','response_format':{'type':'json_object'},'max_tokens':400,'temperature':0.2,
  'messages':[{'role':'user','content':[{'type':'text','text':PROMPT.format(ref=ref)},{'type':'image_url','image_url':{'url':'data:image/jpeg;base64,'+img}}]}]}
 for a in range(4):
  try:
   r=json.load(urllib.request.urlopen(urllib.request.Request('https://openrouter.ai/api/v1/chat/completions',data=json.dumps(body).encode(),headers={'Authorization':'Bearer '+KEY,'Content-Type':'application/json'}),timeout=90))
   txt=r['choices'][0]['message']['content'];j=json.loads(txt[txt.index('{'):txt.rindex('}')+1]);return {'caption':str(j.get('caption','')).strip(),'people':[str(p) for p in j.get('people',[]) if p]}
  except Exception as e:err=e;time.sleep(3+a*5)
 return {'caption':'','people':[],'error':str(err)[:80]}
def work(title):
 res=ask(title)
 with lock:
  caps[title]=res
  if len(caps)%25==0:OUT.write_text(json.dumps(caps,indent=0));print(len(caps),flush=True)
def main():
 files=[f for f in json.load(open(D/'files.json')) if f not in caps or not caps[f].get('caption')]
 print('to caption:',len(files),flush=True)
 with concurrent.futures.ThreadPoolExecutor(8) as ex:list(ex.map(work,files))
 OUT.write_text(json.dumps(caps,indent=0));bad=[k for k,v in caps.items() if not v.get('caption')];print('done',len(caps),'empty',len(bad))
if __name__=='__main__':main()
