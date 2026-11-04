"""Create an original-style 48px mouse through native coordinate operations."""
import asyncio, hashlib, json
from native_session import Native

def frame(i):
 layers=[{}, {}, {}]
 def dot(l,x,y,c):
  if 0<=x<48 and 0<=y<48:layers[l][(int(x),int(y))]=c
 def ellipse(l,cx,cy,rx,ry,c):
  for y in range(cy-ry,cy+ry+1):
   for x in range(cx-rx,cx+rx+1):
    if ((x-cx)/rx)**2+((y-cy)/ry)**2<=1:dot(l,x,y,c)
 def line(l,x,y,X,Y,c,w=1):
  n=max(abs(X-x),abs(Y-y),1)
  for k in range(n+1):
   a=round(x+(X-x)*k/n);b=round(y+(Y-y)*k/n)
   for dy in range(w):
    for dx in range(w):dot(l,a+dx,b+dy,c)
 dark='#3a2420';fur='#c99560';light='#dfad79';cream='#f4e2b8';pink='#e98d9c';red='#c9443c'
 bob=[0,1,0,0,1,0][i]; stride=[0,2,-2,1,-1,2][i]
 # Tail remains behind body. All shapes are authored on live layers.
 line(0,13,32+bob,5,35+bob,'#9c6a57',2);line(0,5,35+bob,2,31+bob,'#9c6a57',2)
 ellipse(0,21+stride//2,42,6,3,dark);ellipse(0,31-stride//2,42,6,3,dark)
 ellipse(0,21+stride//2,41,4,2,'#9c6a3e');ellipse(0,31-stride//2,41,4,2,'#9c6a3e')
 ellipse(0,23,32+bob,11,12,dark);ellipse(1,23,31+bob,9,10,fur)
 ellipse(1,27,32+bob,5,8,cream)
 # Large ears, head and right-facing muzzle echo the preserved character.
 ellipse(0,13,10+bob,8,9,dark);ellipse(1,13,10+bob,6,7,fur);ellipse(1,13,10+bob,4,5,pink)
 ellipse(0,30,9+bob,7,8,dark);ellipse(1,30,9+bob,5,6,fur);ellipse(1,30,9+bob,3,4,pink)
 ellipse(0,24,19+bob,13,12,dark);ellipse(1,24,18+bob,11,10,fur)
 ellipse(1,29,22+bob,9,6,cream);ellipse(1,36,23+bob,5,4,cream)
 ellipse(2,39,21+bob,2,2,'#d4566a');ellipse(2,30,17+bob,2,3,'#1c1014')
 dot(2,29,16+bob,'#fff3dc');line(2,33,25+bob,37,25+bob,'#865c4d')
 line(2,36,24+bob,44,23+bob,'#76534b');line(2,36,26+bob,44,28+bob,'#76534b')
 # Scarf wraps around the neck; a hanging end and pale highlights add detail.
 line(2,12,27+bob,32,28+bob,'#8a2a2a',4);line(2,13,27+bob,30,27+bob,red,2)
 line(2,14,30+bob,13,37+bob,'#8a2a2a',4);line(2,15,31+bob,14,36+bob,red,2)
 line(2,16,27+bob,23,27+bob,'#f07058');line(2,16,31+bob,16,35+bob,'#ef725b')
 ellipse(2,14,33+bob,3,4,fur);ellipse(2,32,32+bob,3,4,fur)
 line(2,17,13+bob,19,11+bob,light);line(2,19,20+bob,23,18+bob,light)
 line(2,18,34+bob,20,37+bob,light);dot(2,12,8+bob,'#ffd3c3')
 return layers

async def main():
 n=Native('mouse-v5-refined')
 try:
  old=await n.start();p='mouse-refined-48'
  await n.call('create_project',project=p,width=48,height=48)
  await n.call('add_layer',project=p,name='Fur and face');await n.call('add_layer',project=p,name='Scarf and detail')
  for i in range(6):
   if i:
    await n.call('add_frame',project=p)
    b=await n.call('read_pixels',project=p,frame=i,layer=0,items=[{'x':23,'y':32}]);assert b['pixels'][0]['color'].lower()=='#00000000'
   for layer,coords in enumerate(frame(i)):await n.paint(p,i,layer,coords)
  await n.call('tag_action',project=p,action='walk',start=0,end=5)
  await n.call('save_project',project=p);await n.call('export_atlas',project=p)
  q=n.root/'projects'/p;sha=hashlib.sha256((q/'atlas.png').read_bytes()).hexdigest()
  await n.stop();fresh=await n.start();assert fresh['boot_id']!=old['boot_id']
  info=await n.call('open_project',project=p);await n.call('export_atlas',project=p)
  assert hashlib.sha256((q/'atlas.png').read_bytes()).hexdigest()==sha
  (n.root/'manifest.json').write_text(json.dumps({'creation':old,'fresh':fresh,'project':info,'atlas_sha256':sha},indent=2))
  print('REFINED_MOUSE_FRESH_REOPEN_PASS',flush=True)
 finally:await n.stop()
if __name__=='__main__':asyncio.run(main())
