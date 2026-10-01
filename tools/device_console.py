#!/usr/bin/env python3
"""Inspect Pixel League over USB; commands u/d/s/b/h/r, ?, capture, record, key, ask, say, bible, weather, location, or update."""
import argparse,time,serial,pathlib
p=argparse.ArgumentParser();p.add_argument('command',nargs='?',default='?');p.add_argument('--port',default='/dev/cu.usbmodem1101');p.add_argument('--seconds',type=float,default=8);p.add_argument('--text');args=p.parse_args()
class PassiveSerial(serial.Serial):
 def _update_dtr_state(self): pass
 def _update_rts_state(self): pass
s=PassiveSerial();s.port=args.port;s.baudrate=115200;s.timeout=.1;s.dtr=False;s.rts=False;s.open();time.sleep(.3);s.reset_input_buffer()
if args.command=='capture':
 s.write(b'p');lines=[];started=False;deadline=time.monotonic()+30
 while time.monotonic()<deadline:
  line=s.readline().decode(errors='replace').strip()
  if line=='FRAME_BEGIN':started=True;continue
  if line=='FRAME_END':break
  if started:lines.append(line)
 data=bytes.fromhex(''.join(lines));assert len(data)==48000,len(data)
 from PIL import Image
 pathlib.Path('previews').mkdir(exist_ok=True)
 Image.frombytes('1',(800,480),data).transpose(Image.Transpose.ROTATE_270).save('previews/device.png');print('Saved previews/device.png')
elif args.command=='record':
 s.write(b'v');lines=[];started=False;deadline=time.monotonic()+40;count=0
 while time.monotonic()<deadline:
  line=s.readline().decode(errors='replace').strip()
  if line.startswith('PCM_BEGIN'):started=True;count=int(line.split()[1]);continue
  if line=='PCM_END':break
  if line=='PCM_FAIL':raise SystemExit('microphone did not start')
  if started and line:lines.append(line)
 data=bytes.fromhex(''.join(lines));assert len(data)==count,(len(data),count)
 import struct,array
 pcm=array.array('h',data);peak=max(abs(x) for x in pcm) if pcm else 0;rms=(sum(x*x for x in pcm)/len(pcm))**0.5 if pcm else 0
 hdr=b'RIFF'+struct.pack('<I',36+len(data))+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,16000,32000,2,16)+b'data'+struct.pack('<I',len(data))
 pathlib.Path('previews').mkdir(exist_ok=True);pathlib.Path('previews/mic.wav').write_bytes(hdr+data)
 print(f'Saved previews/mic.wav: {len(pcm)/16000:.2f} s, peak {peak} ({peak/327.67:.1f}% FS), rms {rms:.0f}')
elif args.command=='key':
 import re,os
 env=pathlib.Path(os.path.expanduser('~/.hermes/.env')).read_text();key=re.search(r'OPENROUTER_API_KEY=(sk-or-v1-[A-Za-z0-9]+)',env).group(1)
 s.write(b'K'+key.encode()+b'\n');deadline=time.monotonic()+3
 while time.monotonic()<deadline:
  line=s.readline()
  if line:print(line.decode(errors='replace').rstrip())
elif args.command=='location': # set the weather location (ZIP or city) and refetch
 s.write(b'L'+(args.text or '').encode()+b'\n');s.write(b'W');deadline=time.monotonic()+40
 while time.monotonic()<deadline:
  line=s.readline()
  if line:
   text=line.decode(errors='replace').rstrip();print(text)
   if text.startswith('FETCH weather'):break
elif args.command=='update': # check the latest GitHub release and install it if newer
 s.write(b'U');deadline=time.monotonic()+180
 while time.monotonic()<deadline:
  line=s.readline()
  if line:
   text=line.decode(errors='replace').rstrip();print(text)
   if text.startswith('FETCH update') or text.startswith('UPDATE restarting'):break
elif args.command=='weather': # refetch the forecast now and print the result
 s.write(b'W');deadline=time.monotonic()+30
 while time.monotonic()<deadline:
  line=s.readline()
  if line:
   text=line.decode(errors='replace').rstrip();print(text)
   if text.startswith('FETCH weather'):break
elif args.command=='bible': # open the reader at a reference ("John 3:16", "Psalm 23", or "daily")
 s.write(b'B'+(args.text or 'daily').encode()+b'\n');deadline=time.monotonic()+6
 while time.monotonic()<deadline:
  line=s.readline()
  if line:
   text=line.decode(errors='replace').rstrip();print(text)
   if text.startswith('BIBLE open'):break
elif args.command=='say':
 s.write(b'S'+(args.text or 'The Bears beat the Eagles twenty seven to seven on Monday night, and the Broncos edged the Rams thirty to twenty six.').encode()+b'\n');deadline=time.monotonic()+40
 while time.monotonic()<deadline:
  line=s.readline()
  if line:
   text=line.decode(errors='replace').rstrip();print(text)
   if text.startswith('SPEAK '):break
elif args.command=='ask':
 s.write(b'a');deadline=time.monotonic()+max(args.seconds,25)
 while time.monotonic()<deadline:
  line=s.readline()
  if line:
   text=line.decode(errors='replace').rstrip();print(text)
   if text.startswith('SPEAK ') or text.startswith('VOICE attempt') and 'failed' in text:break
   if text.startswith('VOICE http=') and 'ok=0' in text:break
else:
 s.write(args.command.encode());deadline=time.monotonic()+args.seconds
 while time.monotonic()<deadline:
  line=s.readline()
  if line:print(line.decode(errors='replace').rstrip())
s.close()
