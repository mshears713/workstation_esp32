import asyncio,json,hashlib,math
from native_session import Native,BASE
ns={};exec((BASE/'native_landmarks.py').read_text().split("begin('center-castle'")[0],ns)
begin,dot,rect,line,ellipse,tri=[ns[k] for k in ['begin','dot','rect','line','ellipse','tri']]
# Shared sampled path drives both authored rails and runtime train position.
controls=[(9,96),(17,84),(36,54),(52,29),(65,18),(80,20),(96,34),(99,52),(90,70),(72,77),(55,68),(49,53),(57,40),(71,38),(84,50),(91,75),(103,97)]
points=[]
for i in range(len(controls)-1):
 a=controls[max(0,i-1)];b=controls[i];c=controls[i+1];d=controls[min(len(controls)-1,i+2)]
 for j in range(8):
  t=j/8
  points.append(tuple(round(.5*((2*b[k])+(-a[k]+c[k])*t+(2*a[k]-5*b[k]+4*c[k]-d[k])*t*t+(-a[k]+3*b[k]-3*c[k]+d[k])*t*t*t)) for k in [0,1]))
points.append(controls[-1])
begin('hulk-route',110,112,['Supports and launch tunnel','Continuous green rails','Cross ties'])
for x,y in [(36,54),(55,28),(80,22),(98,42),(72,77)]:line(0,x,y,x,108,'#6E8580',2)
rect(0,0,85,25,25,'#526F60');rect(0,0,81,24,5,'#78937A');rect(0,5,88,15,22,'#263C3E')
for i,(x,y) in enumerate(points):
 n=points[min(i+1,len(points)-1)];p=points[max(i-1,0)];dx=n[0]-p[0];dy=n[1]-p[1];norm=max(1,math.hypot(dx,dy));nx=-dy/norm;ny=dx/norm
 for offset in [-3,3]:
  xx=round(x+nx*offset);yy=round(y+ny*offset);rect(1,xx-1,yy-1,3,3,'#354D46');dot(1,xx,yy,'#9BB77A')
 if i%5==0:line(2,round(x-nx*3),round(y-ny*3),round(x+nx*3),round(y+ny*3),'#789B64')
begin('return-tunnel',22,29,['Dark opening','Stone green surround'])
rect(0,0,0,22,29,'#263C3E');rect(1,0,0,22,4,'#9AA88A');rect(1,0,4,4,25,'#526F60');rect(1,18,4,4,25,'#526F60')
begin('castle-silhouette',32,40,['Round ears and silhouette','Paw silhouette'])
ellipse(0,8,8,7,7,'#202B32');ellipse(0,24,8,7,7,'#202B32');ellipse(0,16,19,11,12,'#202B32');ellipse(0,18,31,8,8,'#202B32');ellipse(1,5,30,4,3,'#202B32')
begin('window-princess-v3',18,28,['Hair and gown silhouette','Face and crown','Bodice and sleeve detail'])
ellipse(0,9,8,6,7,'#B99356');rect(0,3,8,12,8,'#B99356');tri(0,9,13,17,15,'#9D7F9D')
ellipse(1,9,9,4,5,'#E8C5A0');line(1,5,2,13,2,'#E5CC83');rect(1,5,0,2,3,'#E5CC83');rect(1,8,0,2,3,'#E5CC83');rect(1,12,0,2,3,'#E5CC83')
dot(1,7,8,'#3D3E49');dot(1,11,8,'#3D3E49');line(1,8,11,10,11,'#AD7776')
rect(2,6,14,7,5,'#C7ACBA');line(2,4,16,2,20,'#E8C5A0',2);line(2,13,16,16,19,'#E8C5A0');line(2,9,19,6,25,'#DAC6CE');line(2,4,26,14,26,'#DAC6CE')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
async def main():
 n=Native('guests-track-v3');meta=[]
 try:
  old=await n.start()
  for name,w,h,names,layers in ns['projects']:
   await n.call('create_project',project=name,width=w,height=h)
   for name2 in names[1:]:await n.call('add_layer',project=name,name=name2)
   for i,coords in enumerate(layers):await n.paint(name,0,i,coords)
   await n.call('save_project',project=name);await n.call('export_frame',project=name,frame=0)
   p=n.root/'projects'/name;meta.append(dict(project=name,width=w,height=h,semantic_layers=names,info=await n.call('project_info',project=name),pxo_sha256=sha(p/(name+'.pxo')),png_sha256=sha(p/'frame_000.png')))
  await n.stop();fresh=await n.start();assert fresh['boot_id']!=old['boot_id']
  for m in meta:
   assert await n.call('open_project',project=m['project'])==m['info'];await n.call('export_frame',project=m['project'],frame=0)
   assert sha(n.root/'projects'/m['project']/'frame_000.png')==m['png_sha256']
  (BASE/'v3-guests-manifest.json').write_text(json.dumps(dict(initial=old,fresh=fresh,projects=meta,fresh_reopen_exports_match=True),indent=2))
  (BASE/'v3-route.json').write_text(json.dumps(points));print('V3_GUESTS_ROUTE_FRESH_REOPEN_PASS',flush=True)
 finally:await n.stop()
if __name__=='__main__':asyncio.run(main())
