"""Render real HOME code and check outside/doorway/inside/exit occlusion.

Uses the simulator's uncompressed RGB output, so no image library is needed.
"""
import json,re,subprocess,pathlib
repo=pathlib.Path(__file__).resolve().parents[3]
times={'outside':2200,'threshold':2700,'inside':5000,'exit':10000}
frames={};nodes={}
for name,ms in times.items():
    result=subprocess.run(['/opt/workstation/lvgl-sim/bin/ws-ui','--repo',str(repo),'render','home','--advance-ms',str(ms)],text=True,capture_output=True,check=True)
    match=re.search(r'files: (\S+)/screen.png',result.stdout)
    assert match,result.stdout
    path=pathlib.Path(match.group(1));frames[name]=(path/'screen.rgb').read_bytes()
    tree=json.loads((path/'tree.json').read_text()); nodes[name]=tree[-2]
    print(name,ms,'ms',path,flush=True)
assert not nodes['outside']['hidden'] and nodes['inside']['hidden'] and not nodes['exit']['hidden']
assert not nodes['outside']['clickable'] and not nodes['exit']['clickable']
def changed(a,b,x1,x2):
    count=0
    for y in range(124,156):
        for x in range(x1,x2):
            if 287 <= x < 291 and 138 <= y < 143: continue # lamp is intentionally different
            i=(y*320+x)*3
            count+=frames[a][i:i+3]!=frames[b][i:i+3]
    return count
outside=changed('outside','inside',276,296)
past_door=changed('threshold','inside',296,316)
exit_pixels=changed('exit','inside',276,296)
assert outside>25, f'outside sprite did not draw over facade: {outside}'
assert past_door==0, f'doorway transition leaked beyond doorway: {past_door}'
assert exit_pixels>25, f'exiting sprite did not draw over facade: {exit_pixels}'
print(f'VISUAL PASS: outside={outside}, beyond-door={past_door}, exit={exit_pixels}; inside hidden; sprite non-clickable')
