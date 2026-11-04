"""Native end tunnel obscures the launch car after the crest."""
import asyncio,hashlib,json
from native_session import Native
L=[{},{}]
for y in range(30):
 for x in range(26):
  if y>=10 or ((x-13)/13)**2+((y-10)/10)**2<=1:
   L[0][(x,y)]='#456653'
  if y>=14 and 5<=x<=21 or (y<14 and ((x-13)/8)**2+((y-14)/9)**2<=1):
   L[1][(x,y)]='#203b36'
for x in range(4,22):L[1][(x,28)]='#8aac62'
async def main():
 n=Native('tunnel-v5')
 try:
  old=await n.start();p='launch-end';await n.call('create_project',project=p,width=26,height=30)
  await n.call('add_layer',project=p,name='Opening')
  for i,c in enumerate(L):await n.paint(p,0,i,c)
  await n.call('save_project',project=p);await n.call('export_frame',project=p,frame=0)
  q=n.root/'projects'/p/'frame_000.png';sha=hashlib.sha256(q.read_bytes()).hexdigest()
  await n.stop();fresh=await n.start();assert fresh['boot_id']!=old['boot_id']
  info=await n.call('open_project',project=p);await n.call('export_frame',project=p,frame=0)
  assert hashlib.sha256(q.read_bytes()).hexdigest()==sha
  (n.root/'manifest.json').write_text(json.dumps({'creation':old,'fresh':fresh,'project':info,'sha256':sha},indent=2))
  print('TUNNEL_FRESH_REOPEN_PASS',flush=True)
 finally:await n.stop()
if __name__=='__main__':asyncio.run(main())
