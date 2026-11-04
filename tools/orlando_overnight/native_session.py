import asyncio, hashlib, json, os, shutil, subprocess, sys, time
from pathlib import Path
from fastmcp import Client
from fastmcp.client.transports import StdioTransport

BASE=Path('/home/mshears/orlando-home-20261103')
PY='/home/mshears/pixelorama-workshop-20261025/.venv/bin/python'
BIN='/home/mshears/pixelorama-workshop-20261025/vendor/Pixelorama-Linux-64bit/Pixelorama.x86_64'

class Native:
 def __init__(self,group):
  self.root=BASE/group; self.proc=None; self.client=None; self.count=0
  self.root.mkdir(exist_ok=True)
  self.log=open(self.root/'native-operations.jsonl','a',buffering=1)
 async def start(self):
  for p in ['mailbox/inbox','mailbox/outbox','mailbox/work','projects','profile/pixelorama/extensions','profile/config','profile/cache']:
   (self.root/p).mkdir(parents=True,exist_ok=True)
  shutil.copy2(BASE/'dist/WorkshopBridge.zip',self.root/'profile/pixelorama/extensions/WorkshopBridge.zip')
  (self.root/'profile/pixelorama/config.ini').write_text('[extensions]\nWorkshopBridge=true\n')
  env=dict(os.environ,PIXELORAMA_WORKSHOP_ROOT=str(self.root),XDG_DATA_HOME=str(self.root/'profile'),XDG_CONFIG_HOME=str(self.root/'profile/config'),XDG_CACHE_HOME=str(self.root/'profile/cache'),PYTHONPATH=str(BASE/'src'))
  self.processlog=open(self.root/f'backend-{time.time_ns()}.log','w')
  self.proc=subprocess.Popen([BIN,'--headless'],env=env,stdout=self.processlog,stderr=subprocess.STDOUT)
  (self.root/'owned-pid.json').write_text(json.dumps({'pid':self.proc.pid,'root':str(self.root),'started':time.time()}))
  self.client=Client(StdioTransport(command=PY,args=['-m','workshop.server'],env=env,cwd=str(BASE),keep_alive=False),timeout=30)
  await self.client.__aenter__()
  self.status=await self.call('backend_status')
  assert self.status['open_projects']==0,self.status
  return self.status
 async def call(self,toolname,**args):
  result=await self.client.call_tool(toolname,args)
  if result.is_error: raise RuntimeError(str(result))
  data=result.data
  self.log.write(json.dumps({'time':time.time(),'tool':toolname,'args':args,'result':data})+'\n')
  self.count+=1
  return data
 async def stop(self):
  if self.client: await self.client.__aexit__(None,None,None); self.client=None
  if self.proc:
   # Exact subprocess created by this session only; no process-name kill.
   self.proc.terminate(); self.proc.wait(timeout=15); self.processlog.close(); self.proc=None
 async def paint(self,p,f,l,coords):
  items=[{'x':x,'y':y,'color':c if len(c)==9 else c+'ff'} for (x,y),c in coords.items()]
  for i in range(0,len(items),1024): await self.call('put_pixels',project=p,frame=f,layer=l,items=items[i:i+1024])
  if items:
   check=items[::max(1,len(items)//7)][:8]
   actual=await self.call('read_pixels',project=p,frame=f,layer=l,items=[{'x':v['x'],'y':v['y']} for v in check])
   assert [v['color'].lower() for v in check]==[v['color'].lower() for v in actual['pixels']]

async def probe():
 n=Native('probe')
 try:
  status=await n.start(); print(json.dumps(status),flush=True)
  print(json.dumps([{'name':t.name,'schema':t.inputSchema} for t in await n.client.list_tools()]),flush=True)
  p='local-probe'
  await n.call('create_project',project=p,width=8,height=8)
  await n.call('add_layer',project=p,name='Detail')
  await n.paint(p,0,0,{(2,2):'#EEE1B8'})
  await n.call('add_frame',project=p)
  print('NEW_FRAME',await n.call('read_pixels',project=p,frame=1,layer=0,items=[{'x':2,'y':2}]),flush=True)
  await n.paint(p,1,1,{(3,2):'#303C3C'})
  await n.call('tag_action',project=p,action='probe',start=0,end=1)
  print('SAVE',await n.call('save_project',project=p),flush=True)
  await n.call('export_frame',project=p,frame=0)
  await n.call('export_atlas',project=p)
  before=hashlib.sha256((n.root/'projects'/p/'atlas.png').read_bytes()).hexdigest()
  await n.stop(); newer=await n.start(); assert newer['boot_id']!=status['boot_id']
  print('REOPEN',await n.call('open_project',project=p),flush=True)
  await n.call('export_atlas',project=p)
  assert hashlib.sha256((n.root/'projects'/p/'atlas.png').read_bytes()).hexdigest()==before
  print('FRESH REOPEN MATCH',before,flush=True)
 finally: await n.stop()
if __name__=='__main__': asyncio.run(probe())
