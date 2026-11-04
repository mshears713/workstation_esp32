import asyncio,hashlib,json
from native_session import Native
async def main():
 n=Native('van-door-overnight');name='van-entry-door'
 try:
  old=await n.start();await n.call('create_project',project=name,width=15,height=24);await n.call('add_layer',project=name,name='Door frame and threshold');await n.call('add_layer',project=name,name='Interior and handle')
  layers=[{}, {}, {}]
  def rect(l,x,y,w,h,c):
   for yy in range(y,y+h):
    for xx in range(x,x+w):layers[l][(xx,yy)]=c
  rect(0,0,0,15,24,'#312b30');rect(1,1,1,13,22,'#d76f35');rect(2,3,3,9,17,'#263843');rect(2,4,4,7,6,'#547787');rect(1,2,20,11,2,'#ecc58b');rect(2,11,12,2,2,'#f3dda4')
  for i,p in enumerate(layers):await n.paint(name,0,i,p)
  await n.call('save_project',project=name);await n.call('export_frame',project=name,frame=0)
  p=n.root/'projects'/name;digest=hashlib.sha256((p/'frame_000.png').read_bytes()).hexdigest();info=await n.call('project_info',project=name)
  await n.stop();fresh=await n.start();assert fresh['boot_id']!=old['boot_id'];assert await n.call('open_project',project=name)==info
  await n.call('export_frame',project=name,frame=0);assert hashlib.sha256((p/'frame_000.png').read_bytes()).hexdigest()==digest
  (n.root/'manifest.json').write_text(json.dumps({'initial':old,'fresh':fresh,'info':info,'png_sha256':digest},indent=2));print('VAN_DOOR_FRESH_REOPEN_PASS',flush=True)
 finally:await n.stop()
if __name__=='__main__':asyncio.run(main())
