"""Native launch crest: short rail extension and support, not a full loop."""
import asyncio,hashlib,json,math
from native_session import Native
L=[{},{}]
def dot(l,x,y,c):
 if 0<=x<42 and 0<=y<70:L[l][(x,y)]=c
def line(l,x,y,X,Y,c,w=1):
 n=max(abs(X-x),abs(Y-y),1)
 for i in range(n+1):
  a=round(x+(X-x)*i/n);b=round(y+(Y-y)*i/n)
  for yy in range(w):
   for xx in range(w):dot(l,a+xx,b+yy,c)
# The native launch rises from left to a crest, then dips behind greenery.
points=[(0,11),(4,8),(8,6),(12,5),(16,5),(20,6),(24,8),(28,11),(32,15),(36,19),(40,23)]
for x,y in [(7,6),(23,8),(36,19)]:line(0,x,y,x,64,'#56796d',2)
for (x,y),(X,Y) in zip(points,points[1:]):
 line(1,x,y,X,Y,'#244d49',4);line(1,x,y,X,Y,'#83ad57',2)
async def main():
 n=Native('crest-v5b')
 try:
  old=await n.start();p='launch-crest';await n.call('create_project',project=p,width=42,height=70)
  await n.call('add_layer',project=p,name='Rails')
  for i,c in enumerate(L):await n.paint(p,0,i,c)
  await n.call('save_project',project=p);await n.call('export_frame',project=p,frame=0)
  q=n.root/'projects'/p/'frame_000.png';sha=hashlib.sha256(q.read_bytes()).hexdigest()
  await n.stop();fresh=await n.start();assert fresh['boot_id']!=old['boot_id']
  info=await n.call('open_project',project=p);await n.call('export_frame',project=p,frame=0)
  assert hashlib.sha256(q.read_bytes()).hexdigest()==sha
  (n.root/'manifest.json').write_text(json.dumps({'creation':old,'fresh':fresh,'project':info,'sha256':sha},indent=2))
  print('CREST_FRESH_REOPEN_PASS',flush=True)
 finally:await n.stop()
if __name__=='__main__':asyncio.run(main())
