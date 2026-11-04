"""Native Pixelorama coordinate commands for new landmarks only.
Original house, mouse, van and Tour A background are never imported or edited.
"""
import asyncio,math,json,hashlib
from native_session import Native,BASE
projects=[]
def begin(name,w,h,names):
 global L,W,H
 W,H=w,h;L=[{} for _ in names];projects.append((name,w,h,names,L))
def dot(l,x,y,c):
 if 0<=x<W and 0<=y<H:L[l][(int(x),int(y))]=c
def rect(l,x,y,w,h,c):
 for yy in range(y,y+h):
  for xx in range(x,x+w):dot(l,xx,yy,c)
def line(l,x,y,X,Y,c,thick=1):
 n=max(abs(X-x),abs(Y-y),1)
 for i in range(n+1):rect(l,round(x+(X-x)*i/n),round(y+(Y-y)*i/n),thick,thick,c)
def ellipse(l,x,y,rx,ry,c):
 for yy in range(y-ry,y+ry+1):
  for xx in range(x-rx,x+rx+1):
   if ((xx-x)/rx)**2+((yy-y)/ry)**2<=1:dot(l,xx,yy,c)
def tri(l,x,y,w,h,c):
 for yy in range(h):
  r=round(w*yy/h/2);rect(l,x-r,y+yy,r*2+1,1,c)

begin('center-castle',96,116,['Stone silhouette','Blue roofs','Masonry and light','Window openings'])
stone='#B9B3B0';shadow='#827F8C';light='#DED5BC';roof='#59677E';ink='#26384D'
rect(0,13,65,70,48,stone);rect(0,7,108,83,6,shadow)
rect(0,9,53,16,57,light);rect(0,71,53,16,57,light)
rect(0,36,36,24,75,light);rect(0,42,18,12,34,stone)
for x,y,w,h in [(17,30,26,25),(79,30,26,25),(48,2,20,19),(48,20,34,20),(32,57,18,13),(64,57,18,13)]:tri(1,x,y,w,h,roof)
for x,y,h in [(10,55,52),(72,55,52),(37,40,67)]:rect(2,x,y,2,h,'#EEE3C8')
for y in range(73,109,9):
 for x in range(28+(y%2)*4,70,14):line(2,x,y,x+9,y,'#9B969F')
for x in [12,22,72,82]:rect(2,x,61,2,7,'#D6CAA9')
# Central princess window (position reused by the runtime clip).
ellipse(3,48,73,9,12,ink);rect(3,39,73,19,17,ink);rect(2,37,90,23,3,light)
ellipse(3,48,106,9,12,ink);rect(3,39,105,19,9,ink)
for x in [15,77]:rect(3,x,72,5,13,ink)
for x,y in [(17,23),(79,23),(48,0)]:line(2,x,y,x,y+8,'#D9C49D');rect(2,x+1,y,6,3,'#B47779')

begin('eye-wheel',80,80,['Rim and spokes','Gondolas and hub'])
for a in range(360):
 t=math.radians(a);x=round(40+35*math.cos(t));y=round(40+35*math.sin(t))
 dot(0,x,y,'#E7DDC5');dot(0,round(40+34*math.cos(t)),round(40+34*math.sin(t)),'#AAB9BB')
for a in range(0,360,30):
 t=math.radians(a);x=round(40+35*math.cos(t));y=round(40+35*math.sin(t))
 line(0,40,40,x,y,'#C0CDCB')
 rect(1,x-4,y-3,8,6,'#35495B');rect(1,x-3,y-2,6,3,'#DECFA9')
ellipse(1,40,40,4,4,'#E7DDC5');ellipse(1,40,40,2,2,'#4A6576')

begin('eye-base',60,40,['Support legs','Platform'])
line(0,30,0,10,36,'#B5BEB7',3);line(0,30,0,48,36,'#B5BEB7',3)
line(0,30,4,30,36,'#6D8390',2);rect(1,3,35,54,5,'#8C9692')

begin('hulk-launch',108,96,['Launch structure','Green rails','Cross ties and detail'])
ink='#35484D';green='#83A16C';dark='#4D705C'
# Compact tunnel in front of the rising launch track.
for x,y in [(34,58),(51,45),(69,31),(86,18)]:line(0,x,y,x,91,'#788D88',2)
line(1,13,79,94,14,ink,5);line(1,13,77,94,12,green,2)
line(1,17,85,98,20,ink,4);line(1,17,83,98,18,green,2)
for i in range(0,9):
 x=17+i*9;y=77-i*7;line(2,x,y,x+4,y+7,'#B2BD88')
rect(0,0,62,33,30,'#5A7266');tri(0,16,48,38,15,'#405B54')
rect(2,7,68,19,24,'#243C43');rect(2,5,64,23,4,'#A7B18A')
rect(2,1,89,34,5,'#A1A18B')

begin('peek-mouse',32,40,['Silhouette','Face and paws'])
ellipse(0,8,8,7,7,'#25303C');ellipse(0,24,8,7,7,'#25303C');ellipse(0,16,20,11,14,'#25303C')
ellipse(1,16,23,7,9,'#B8AC98');ellipse(1,12,19,2,3,'#EFE2BF');ellipse(1,20,19,2,3,'#EFE2BF')
dot(1,12,20,'#25303C');dot(1,20,20,'#25303C');ellipse(1,16,25,3,2,'#25303C')
rect(0,8,31,17,9,'#25303C');ellipse(1,5,34,4,3,'#B8AC98');ellipse(1,27,34,4,3,'#B8AC98')

begin('window-princess',16,24,['Dress and hair','Face crown and waving hand'])
ellipse(0,8,9,6,8,'#A88654');tri(0,8,13,16,11,'#B18BAA')
ellipse(1,8,9,4,5,'#EAD1AD');dot(1,6,9,'#354354');dot(1,10,9,'#354354')
rect(1,4,3,8,3,'#E5CA83');rect(1,4,1,2,4,'#E5CA83');rect(1,7,0,2,5,'#E5CA83');rect(1,10,1,2,4,'#E5CA83')
line(1,11,16,14,11,'#EAD1AD',2);rect(1,14,9,2,3,'#EAD1AD')

begin('coaster-car',28,16,['Car and seats','Riders and wheels'])
rect(0,1,7,26,6,'#48675B');rect(0,2,7,24,2,'#A6B57B')
for x in [3,11,19]:rect(0,x,4,6,5,'#2B4149');ellipse(1,x+3,4,2,3,'#D8BB96')
for x in [6,21]:ellipse(1,x,13,3,2,'#2B4149')
line(1,4,0,4,4,'#D8BB96');line(1,22,0,22,4,'#D8BB96')

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
async def main():
 n=Native('landmarks-v2');meta=[]
 try:
  old=await n.start()
  for name,w,h,names,layers in projects:
   await n.call('create_project',project=name,width=w,height=h)
   for label in names[1:]:await n.call('add_layer',project=name,name=label)
   for i,coords in enumerate(layers):await n.paint(name,0,i,coords)
   await n.call('save_project',project=name);await n.call('export_frame',project=name,frame=0)
   p=n.root/'projects'/name
   meta.append({'project':name,'width':w,'height':h,'semantic_layers':names,'info':await n.call('project_info',project=name),'pxo_sha256':sha(p/(name+'.pxo')),'png_sha256':sha(p/'frame_000.png')})
   print('SAVED',name,flush=True)
  await n.stop();fresh=await n.start();assert fresh['boot_id']!=old['boot_id']
  for m in meta:
   assert await n.call('open_project',project=m['project'])==m['info']
   await n.call('export_frame',project=m['project'],frame=0)
   assert sha(n.root/'projects'/m['project']/'frame_000.png')==m['png_sha256']
  (BASE/'v2-art-manifest.json').write_text(json.dumps({'initial':old,'fresh':fresh,'projects':meta,'fresh_reopen_exports_match':True},indent=2))
  print('V2_FRESH_REOPEN_PASS',flush=True)
 finally:await n.stop()
if __name__=='__main__':asyncio.run(main())
