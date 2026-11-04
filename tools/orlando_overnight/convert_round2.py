from pathlib import Path
from PIL import Image
import json,hashlib
B=Path(__file__).parent
s=(B/'convert_scene.py').read_text().replace("'background-overnight'","'background-overnight2'");exec(compile(s,str(B/'convert_scene.py'),'exec'),{'__file__':str(B/'convert_scene.py')})
p=B/'mouse-overnight/projects/mouse-overnight-48/atlas.png';m=json.loads((B/'mouse-overnight/manifest.json').read_text());assert hashlib.sha256(p.read_bytes()).hexdigest()==m['atlas_sha256']
im=Image.open(p).convert('RGBA');assert im.size==(288,48);out=['#pragma once','#include "lvgl.h"']
for f in range(6):
 raw=im.crop((48*f,0,48*(f+1),48)).tobytes('raw','BGRA');out.append('static const uint8_t mouse%d_data[]={'%f+','.join(map(str,raw))+'};');out.append('static const lv_image_dsc_t mouse%d={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=48,.h=48,.stride=192},.data_size=sizeof(mouse%d_data),.data=mouse%d_data};'%(f,f,f))
out.append('static const lv_image_dsc_t *orlando_mouse_frames[]={'+','.join('&mouse%d'%f for f in range(6))+'};');(B/'orlando_v5_mouse.h').write_text('\n'.join(out))
m=json.loads((B/'sign-overnight2/manifest.json').read_text());out=['#pragma once','#include "lvgl.h"']
for name,cname in [('guide-sign','guide_sign'),('house-home-plaque','home_plaque')]:
 p=B/'sign-overnight2/projects'/name/'frame_000.png';assert hashlib.sha256(p.read_bytes()).hexdigest()==m['sha256'][name];im=Image.open(p).convert('RGBA');w,h=im.size;raw=im.tobytes('raw','BGRA')
 out.append('static const uint8_t '+cname+'_data[]={'+','.join(map(str,raw))+'};');out.append(f'static const lv_image_dsc_t {cname}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w={w},.h={h},.stride={w*4}}},.data_size=sizeof({cname}_data),.data={cname}_data}};')
(B/'orlando_sign_overnight.h').write_text('\n'.join(out))
