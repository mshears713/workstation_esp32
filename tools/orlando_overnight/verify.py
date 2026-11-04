from pathlib import Path
import subprocess,re,shutil,zlib,struct
B=Path('/home/mshears/orlando-home-20261103');F=B/'fw-overnight';E=B/'overnight-evidence';E.mkdir(exist_ok=True)
def run(args,name):
 r=subprocess.run(args,cwd=F,capture_output=True,text=True);(E/(name+'.log')).write_text(r.stdout+r.stderr);print(name,r.returncode,flush=True)
 if r.returncode:raise RuntimeError(name)
 m=re.search(r'files: (.*?)/screen.png',r.stdout)
 if m:
  for f in ['screen.png','checks.json','log.txt','tree.json']:shutil.copy2(Path(m.group(1))/f,E/(name+'-'+f))
base=['/opt/workstation/lvgl-sim/bin/ws-ui','--repo',str(F),'render','orlando_overnight']
for action,ms in [('default',500),('home',0),('guide',0),('castle',1000),('wheel',500),('coaster',1000),('van',1000),('van',2400),('default',9500),('test',8000)]:
 name='sign' if ms==9500 else action+(('-'+str(ms)) if action=='van' else '')
 run(base+['--fixture',action,'--advance-ms',str(ms)],name)
for action,ms in [('coaster',1500),('castle',2700),('van',3500),('wheel',3300),('default',14200)]:
 name='cycle-'+action;d=E/name;d.mkdir(exist_ok=True)
 run(base+['--fixture',action,'--set','capture_dir='+str(d),'--advance-ms',str(ms)],name)
def chunk(t,d):return struct.pack('!I',len(d))+t+d+struct.pack('!I',zlib.crc32(t+d)&0xffffffff)
for p in E.glob('cycle-*/*.rgb'):
 raw=p.read_bytes();assert len(raw)==320*240*3;scan=b''.join(b'\0'+raw[y*960:(y+1)*960] for y in range(240))
 p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',320,240,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))
print('PNG frames',len(list(E.glob('cycle-*/*.png'))),flush=True)
p=F/'tools/hosttest/.run-lf-overnight.sh';p.write_bytes((p.parent/'run.sh').read_bytes().replace(b'\r\n',b'\n'))
try:run(['bash',str(p)],'host-tests')
finally:p.unlink()
