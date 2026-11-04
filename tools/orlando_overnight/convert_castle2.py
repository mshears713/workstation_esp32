from pathlib import Path
from PIL import Image
import json,hashlib
B=Path(__file__).parent;root=B/'castle-overnight2';m=json.loads((root/'manifest.json').read_text());out=['#pragma once','#include "lvgl.h"']
for p in m['projects']:
 name=p['project'];c=name.replace('-','_');f=root/'projects'/name/'frame_000.png';assert hashlib.sha256(f.read_bytes()).hexdigest()==p['sha256'];im=Image.open(f).convert('RGBA');w,h=im.size;raw=im.tobytes('raw','BGRA')
 out.append('static const uint8_t '+c+'_data[]={'+','.join(map(str,raw))+'};');out.append(f'static const lv_image_dsc_t {c}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w={w},.h={h},.stride={w*4}}},.data_size=sizeof({c}_data),.data={c}_data}};')
(B/'orlando_castle_overnight.h').write_text('\n'.join(out))
