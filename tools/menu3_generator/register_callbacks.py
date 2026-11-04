import json,copy
from pathlib import Path
p=Path('/home/mshears/orlando-home-20261103/fw-menu3/tools/uisim/targets.json');d=json.loads(p.read_text());x=copy.deepcopy(d['targets']['menu3_flow']);x['adapter']='tools/uisim/targets/menu3_app_callbacks.c';x['description']='Actual application navigation callbacks with LVGL pages and backend fixture';x['fields']={};x['fixtures']={'default':{}};d['targets']['menu3_app_callbacks']=x;p.write_text(json.dumps(d,indent=2)+'\n')
