"""Native revision to the saved sky tiles, with fresh-process verification."""
import asyncio, hashlib, json
from native_session import Native
from v5_background import colour

def revised(x,y):
    c=colour(x,y)
    if y<32: return '#567f9e'
    if y<64: return '#6992ac'
    if y<96: return '#7fa4b7'
    if y<120: return '#9ab9bf'
    return c

async def main():
 n=Native('background-v5')
 try:
  old=await n.start()
  for col in range(4):
   p=f'background-{col}-0';await n.call('open_project',project=p)
   await n.paint(p,0,0,{(x,y):revised(col*80+x,y) for y in range(120) for x in range(80)})
   await n.call('save_project',project=p);await n.call('export_frame',project=p,frame=0)
  await n.stop();fresh=await n.start();assert old['boot_id']!=fresh['boot_id']
  manifest=json.loads((n.root/'manifest.json').read_text())
  for r in manifest['tiles']:
   p=r['project'];q=n.root/'projects'/p/'frame_000.png'
   digest=hashlib.sha256(q.read_bytes()).hexdigest()
   await n.call('open_project',project=p);await n.call('export_frame',project=p,frame=0)
   assert hashlib.sha256(q.read_bytes()).hexdigest()==digest
   r['sha256']=digest
  manifest['correction_creation']=old;manifest['correction_fresh']=fresh
  (n.root/'manifest.json').write_text(json.dumps(manifest,indent=2))
  print('BACKGROUND_CORRECTION_FRESH_REOPEN_PASS',flush=True)
 finally: await n.stop()
if __name__=='__main__':asyncio.run(main())
