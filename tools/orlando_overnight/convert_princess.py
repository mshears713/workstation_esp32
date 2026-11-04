from pathlib import Path
from PIL import Image
import hashlib,json
B=Path(__file__).parent;root=B/'princess-overnight';p=root/'projects/princess-walk/atlas.png';m=json.loads((root/'manifest.json').read_text());assert hashlib.sha256(p.read_bytes()).hexdigest()==m['atlas_sha256']
im=Image.open(p).convert('RGBA');assert im.size==(36,32);out=['#pragma once','#include "lvgl.h"']
for i in range(2):
 raw=im.crop((i*18,0,(i+1)*18,32)).tobytes('raw','BGRA');out.append(f'static const uint8_t princess_walk_{i}_data[]={{'+','.join(map(str,raw))+'};');out.append(f'static const lv_image_dsc_t princess_walk_{i}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=18,.h=32,.stride=72}},.data_size=sizeof(princess_walk_{i}_data),.data=princess_walk_{i}_data}};')
out.append('static const lv_image_dsc_t *princess_walk_frames[]={&princess_walk_0,&princess_walk_1};');(B/'orlando_princess_overnight.h').write_text('\n'.join(out))
