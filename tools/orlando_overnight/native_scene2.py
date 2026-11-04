import asyncio,json,hashlib,shutil,math
from pathlib import Path
from native_session import Native,BASE
root=BASE/'background-overnight2';root.mkdir(exist_ok=True)
if not (root/'projects').exists():shutil.copytree(BASE/'background-v5/projects',root/'projects')
layers=[{} for _ in range(8)]
def dot(x,y,c):
 if 0<=x<320 and 0<=y<240:layers[(y//120)*4+x//80][(x%80,y%120)]=c
def rect(x,y,w,h,c):
 for yy in range(y,y+h):
  for xx in range(x,x+w):dot(xx,yy,c)
def line(x,y,X,Y,c,w=1):
 n=max(abs(X-x),abs(Y-y),1)
 for i in range(n+1):rect(round(x+(X-x)*i/n),round(y+(Y-y)*i/n),w,w,c)
# Local parking apron only at the van, not a cross-screen road.
rect(209,155,99,38,'#a5af92');rect(211,156,95,2,'#c6ccae')
for x in [217,255,293]:line(x,188,x+7,178,'#d8d8ba',2)
# Distant palms at edges: flat Florida skyline, no mountains.
for x,y in [(25,126),(294,130),(95,131)]:
 line(x,y,x+4,y-40,'#74654c',3);cx=x+4;cy=y-40
 for dx,dy in [(-18,-5),(-13,-11),(-6,-14),(4,-14),(12,-10),(18,-4)]:
  line(cx,cy,cx+dx,cy+dy,'#547d62',2);line(cx+dx,cy+dy,cx+dx+(-3 if dx<0 else 3),cy+dy+5,'#6d9471')
# Low clouds and garden flecks provide restrained depth.
for x,y in [(23,27),(80,41),(214,24),(289,39)]:
 rect(x,y,20,3,'#c9d8d1');rect(x+5,y-3,11,3,'#c9d8d1');rect(x-4,y+2,27,2,'#c9d8d1')
for x,y in [(105,183),(110,197),(192,173),(194,207),(102,222),(224,216),(241,209),(77,226)]:
 rect(x,y,2,2,'#ead5a0');dot(x+2,y+1,'#d5a496')
async def main():
 n=Native('background-overnight2');records=[]
 try:
  old=await n.start()
  for row in range(2):
   for col in range(4):
    p=f'background-{col}-{row}';await n.call('open_project',project=p);await n.call('add_layer',project=p,name='Florida depth and parking')
    info=await n.call('project_info',project=p);idx=len(info['layers'])-1
    await n.paint(p,0,idx,layers[row*4+col]);await n.call('save_project',project=p);await n.call('export_frame',project=p,frame=0)
    q=n.root/'projects'/p;records.append({'project':p,'origin':[col*80,row*120],'info':info,'sha256':hashlib.sha256((q/'frame_000.png').read_bytes()).hexdigest()})
  await n.stop();fresh=await n.start();assert fresh['boot_id']!=old['boot_id']
  for r in records:
   await n.call('open_project',project=r['project']);await n.call('export_frame',project=r['project'],frame=0)
   assert hashlib.sha256((n.root/'projects'/r['project']/'frame_000.png').read_bytes()).hexdigest()==r['sha256']
  (n.root/'manifest.json').write_text(json.dumps({'creation':old,'fresh':fresh,'tiles':records},indent=2));print('OVERNIGHT_BACKGROUND_FRESH_REOPEN_PASS',flush=True)
 finally:await n.stop()
if __name__=='__main__':asyncio.run(main())
