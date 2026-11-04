import asyncio,json,hashlib
from native_session import Native,BASE
outer={};exec((BASE/'native_guests_track.py').read_text().split('def sha(p):')[0],outer);ns=outer['ns']
base=[p for p in ns['projects'] if p[0]=='window-princess-v3'][0]
async def main():
 n=Native('princess-overnight');name='princess-walk'
 try:
  old=await n.start();await n.call('create_project',project=name,width=18,height=32)
  await n.call('add_layer',project=name,name='Face and crown');await n.call('add_layer',project=name,name='Dress and walking shoes')
  for frame in range(2):
   if frame:await n.call('add_frame',project=name)
   layers=[dict(l) for l in base[4]]
   for x in (3,4,5,6,11,12,13,14):
    y=28+(1 if frame and x<9 else 0);layers[2][(x,y)]='#5b4554';layers[2][(x,y+1)]='#302e3e'
   for i,c in enumerate(layers):await n.paint(name,frame,i,c)
  await n.call('tag_action',project=name,action='walk',start=0,end=1);await n.call('save_project',project=name);await n.call('export_atlas',project=name)
  p=n.root/'projects'/name;digest=hashlib.sha256((p/'atlas.png').read_bytes()).hexdigest();info=await n.call('project_info',project=name)
  await n.stop();fresh=await n.start();assert fresh['boot_id']!=old['boot_id'];assert await n.call('open_project',project=name)==info
  await n.call('export_atlas',project=name);assert hashlib.sha256((p/'atlas.png').read_bytes()).hexdigest()==digest
  (n.root/'manifest.json').write_text(json.dumps({'initial':old,'fresh':fresh,'info':info,'atlas_sha256':digest},indent=2));print('PRINCESS_FRESH_REOPEN_PASS',flush=True)
 finally:await n.stop()
if __name__=='__main__':asyncio.run(main())
