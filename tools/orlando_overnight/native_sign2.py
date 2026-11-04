import asyncio,hashlib,json
from native_session import Native
async def main():
 n=Native('sign-overnight2');p='guide-sign';q='house-home-plaque'
 try:
  old=await n.start();await n.call('create_project',project=p,width=64,height=60);await n.call('add_layer',project=p,name='Board lettering and handle');await n.call('add_layer',project=p,name='Paw')
  layers=[{}, {}, {}]
  def rect(l,x,y,w,h,c):
   for yy in range(y,y+h):
    for xx in range(x,x+w):
     if 0<=xx<64 and 0<=yy<60:layers[l][(xx,yy)]=c
  rect(0,1,1,48,23,'#66483f');rect(0,2,2,46,21,'#f3e9cf');
  for i in range(23):rect(1,46+i*13//22,22+i,2,2,'#785945')
  rect(2,56,41,8,8,'#3a2420');rect(2,57,42,6,5,'#f4e2b8');rect(2,58,42,3,2,'#dfad79')
  # Tiny custom pixel letters remain crisp at physical size.
  glyph={'T':['111','010','010','010','010'],'A':['010','101','111','101','101'],'P':['110','101','110','100','100'],'M':['101','111','111','101','101'],'E':['111','100','110','100','111'],'H':['101','101','111','101','101'],'O':['111','101','101','101','111']}
  def word(target,text,x,y,scale,color):
   for ch in text:
    if ch==' ':x+=2*scale;continue
    for yy,row in enumerate(glyph[ch]):
     for xx,v in enumerate(row):
      if v=='1':target(x+xx*scale,y+yy*scale,scale,scale,color)
    x+=4*scale
  word(lambda x,y,w,h,c:rect(1,x,y,w,h,c),'TAP ME',4,7,1,'#263b4b')
  for i,d in enumerate(layers):await n.paint(p,0,i,d)
  await n.call('save_project',project=p);await n.call('export_frame',project=p,frame=0)
  await n.call('create_project',project=q,width=17,height=7);await n.call('add_layer',project=q,name='HOME lettering');d={(x,y):'#f3e9cf' for y in range(7) for x in range(17)}
  def put(x,y,w,h,c):
   for yy in range(y,y+h):
    for xx in range(x,x+w):d[(xx,yy)]=c
  word(put,'HOME',1,1,1,'#263b4b');await n.paint(q,0,1,d)
  await n.call('save_project',project=q);await n.call('export_frame',project=q,frame=0)
  dig={a:hashlib.sha256((n.root/'projects'/a/'frame_000.png').read_bytes()).hexdigest() for a in [p,q]}
  await n.stop();fresh=await n.start();assert fresh['boot_id']!=old['boot_id']
  for a in [p,q]:
   await n.call('open_project',project=a);await n.call('export_frame',project=a,frame=0);assert hashlib.sha256((n.root/'projects'/a/'frame_000.png').read_bytes()).hexdigest()==dig[a]
  (n.root/'manifest.json').write_text(json.dumps({'initial':old,'fresh':fresh,'sha256':dig},indent=2));print('SIGN_FRESH_REOPEN_PASS',flush=True)
 finally:await n.stop()
if __name__=='__main__':asyncio.run(main())
