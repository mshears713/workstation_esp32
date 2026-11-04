import asyncio,json,hashlib
from native_session import Native,BASE
ns={};exec((BASE/'native_landmarks.py').read_text().split("begin('center-castle'")[0],ns)
begin,dot,rect,line,ellipse,tri=[ns[k] for k in ['begin','dot','rect','line','ellipse','tri']]
begin('castle-overnight',96,116,['Stone walls','Roofs and spires','Masonry detail','Windows and arches'])
ink='#263b4b';stone='#c4bcb2';shadow='#8a8992';light='#e6dbc5';roof='#5c728a'
# Broad castle silhouette, taller central tower, and small side towers.
rect(0,7,52,82,62,stone);rect(0,1,111,94,4,shadow)
rect(0,2,39,18,71,light);rect(0,76,39,18,71,light)
rect(0,30,28,36,84,light);rect(0,39,9,18,33,stone)
for x,y,w,h in [(11,13,22,27),(85,13,22,27),(48,0,18,11),(48,11,34,21),(27,35,20,20),(69,35,20,20)]:tri(1,x,y,w,h,roof)
for x in [3,20,75,92]:rect(2,x,42,2,68,'#eee5d3')
for y in range(56,105,10):
 for x in range(22+(y%3)*3,74,16):line(2,x,y,x+9,y,'#aaa3a0')
# Central launch opening aligned at (48,68), plus broad right peeking window.
ellipse(3,48,64,10,12,ink);rect(3,38,64,21,14,ink);rect(2,36,78,25,3,light)
ellipse(3,78,60,8,10,'#8ca9ac');rect(3,70,60,17,12,'#8ca9ac');rect(2,68,72,21,3,light)
ellipse(3,18,62,5,8,ink);rect(3,13,62,11,9,ink)
# Main lower doorway, aligned to a walking princess.
ellipse(3,48,104,9,10,ink);rect(3,39,104,19,10,ink)
for x,y in [(11,13),(85,13),(48,0)]:line(2,x,y,x,y+7,'#e8d4a8');rect(2,x+1,y,6,3,'#b67979')
begin('launch-bridge',80,20,['Rail and ties','Highlights'])
line(0,0,16,76,7,'#354d46',4);line(1,0,15,76,6,'#9db97e',2)
for x in range(4,76,10):
 y=15-x*9//76;line(1,x,y,x+1,y+4,'#c0cb95')
async def main():
 n=Native('castle-overnight2');m=[]
 try:
  old=await n.start()
  for name,w,h,names,layers in ns['projects']:
   await n.call('create_project',project=name,width=w,height=h)
   for a in names[1:]:await n.call('add_layer',project=name,name=a)
   for i,c in enumerate(layers):await n.paint(name,0,i,c)
   await n.call('save_project',project=name);await n.call('export_frame',project=name,frame=0)
   p=n.root/'projects'/name;m.append({'project':name,'info':await n.call('project_info',project=name),'sha256':hashlib.sha256((p/'frame_000.png').read_bytes()).hexdigest()})
  await n.stop();fresh=await n.start();assert fresh['boot_id']!=old['boot_id']
  for q in m:
   assert await n.call('open_project',project=q['project'])==q['info'];await n.call('export_frame',project=q['project'],frame=0)
   assert hashlib.sha256((n.root/'projects'/q['project']/'frame_000.png').read_bytes()).hexdigest()==q['sha256']
  (n.root/'manifest.json').write_text(json.dumps({'initial':old,'fresh':fresh,'projects':m},indent=2));print('CASTLE_FRESH_REOPEN_PASS',flush=True)
 finally:await n.stop()
if __name__=='__main__':asyncio.run(main())
