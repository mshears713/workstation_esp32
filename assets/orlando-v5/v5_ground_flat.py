"""Replace the saved hedge-band pixels with flat sky and grassy ground."""
import asyncio,hashlib,json
from native_session import Native
def color(x,y):
 if y<132:return '#9ab9bf'
 if (x*17+y*31)%181<2:return '#d5c987'
 return '#91ad7a'
async def main():
 n=Native('background-v5')
 try:
  old=await n.start()
  for col in range(4):
   p=f'background-{col}-1';await n.call('open_project',project=p)
   await n.paint(p,0,0,{(x,y):color(col*80+x,120+y) for y in range(120) for x in range(80)})
   await n.call('save_project',project=p);await n.call('export_frame',project=p,frame=0)
  await n.stop();fresh=await n.start();assert fresh['boot_id']!=old['boot_id']
  manifest=json.loads((n.root/'manifest.json').read_text())
  for r in manifest['tiles']:
   p=r['project'];q=n.root/'projects'/p/'frame_000.png';digest=hashlib.sha256(q.read_bytes()).hexdigest()
   await n.call('open_project',project=p);await n.call('export_frame',project=p,frame=0)
   assert hashlib.sha256(q.read_bytes()).hexdigest()==digest;r['sha256']=digest
  manifest['flat_ground_creation']=old;manifest['flat_ground_fresh']=fresh
  (n.root/'manifest.json').write_text(json.dumps(manifest,indent=2))
  print('FLAT_GROUND_FRESH_REOPEN_PASS',flush=True)
 finally:await n.stop()
if __name__=='__main__':asyncio.run(main())
