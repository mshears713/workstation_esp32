import hashlib,json,pathlib,shutil,sys
sys.path.insert(0,'/opt/workstation/lvgl-sim')
from wsui import build as b,png
root=pathlib.Path(__file__).resolve().parents[2]
header=b.kconfig_header(root/'sdkconfig');commit=b._lvgl_commit();flags=' '.join([b.CC,*b.LIB_CFLAGS])
source=None
for p in sorted(b.CACHE.glob('lvgl-*/ws_sdkconfig.h')):
    old=p.read_text()
    if old.splitlines()[1:]==header.splitlines()[1:] and (p.parent/'liblvgl.a').is_file() and b._sha(old.encode(),commit.encode(),flags.encode())==p.parent.name.removeprefix('lvgl-'):
        source=p.parent;break
assert source,'No matching cached LVGL: do not launch a full rebuild'
cache=root/'render-cache';dest=cache/('lvgl-'+b._sha(header.encode(),commit.encode(),flags.encode()));dest.mkdir(parents=True,exist_ok=True)
shutil.copy2(source/'liblvgl.a',dest/'liblvgl.a');(dest/'ws_sdkconfig.h').write_text(header);b.CACHE=cache
tc=b.ensure_toolchain(root);assert not tc.built_now
out=root/'evidence';out.mkdir(exist_ok=True)
shared=[root/'main/lincoln_scene.c',root/'main/ui_lincoln.c',root/'main/lincoln_art.c']
receipt={'draft':True,'native_art_revision':'v2-polish','lvgl_commit':commit,'source_hashes':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*shared,root/'tools/uisim/targets/lincoln_draft.c',root/'tools/uisim/targets/lincoln_ui_test.c']},
 'capture':'Actual C state and LVGL native-image renderer with 20ms timer. Simulated audio start/completion injected from decoded candidate durations; selected ScottW durations; no real speaker, filesystem or microphone.',
 'cache':{'source':str(source),'sha256':hashlib.sha256(tc.lib.read_bytes()).hexdigest()},'scenarios':{}}
test=out/'ui-test';test.mkdir(exist_ok=True)
result=b.compile_and_run(tc,test,[root/'tools/uisim/targets/lincoln_ui_test.c',*shared],[root/'main'],None,0)
print(result.log,flush=True);assert result.ok,result.log
receipt['ui_test']={'ok':True,'log':result.log}
for name,scenario,times in [
    ('full',0,list(range(0,36001,100))),
    ('stop-reading',1,list(range(6300,11001,50))),
    ('stop-preparation',2,list(range(0,8001,100)))]:
    work=out/(name+'-exe');work.mkdir(exist_ok=True)
    adapter=work/'adapter.c';adapter.write_text(f'#define LINCOLN_SIM_SCENARIO {scenario}\n'+(root/'tools/uisim/targets/lincoln_draft.c').read_text())
    result=b.compile_and_run(tc,work,[adapter,*shared],[root/'main'],None,0);assert result.ok,result.log
    frames=out/name;frames.mkdir(exist_ok=True);record=[]
    for ms in times:
        rgb=work/'screen.rgb';tree=work/'tree.json'
        r=b._run([str(work/'lvgl_sim'),'--width','320','--height','240','--advance-ms',str(ms),'--rgb',str(rgb),'--tree',str(tree)],cwd=work,timeout=30,mem_mb=512,cpu_s=25)
        assert r.returncode==0,(name,ms,r.stderr)
        target=frames/f'frame-{ms:05}.png';target.write_bytes(png.encode(rgb.read_bytes(),320,240,1))
        record.append({'ms':ms,'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
        if ms in (0,3500,8000,27000,29000):print(name,ms,'captured',flush=True)
    receipt['scenarios'][name]=record
(out/'receipt.json').write_text(json.dumps(receipt,indent=2))
print('DRAFT CAPTURE PASS',len(receipt['scenarios']['full']),'full-cycle frames; cancellation scenarios also captured',flush=True)
