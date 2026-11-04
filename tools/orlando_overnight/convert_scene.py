from pathlib import Path
import json,hashlib
from PIL import Image
B=Path(__file__).parent;root=B/'background-overnight';m=json.loads((root/'manifest.json').read_text());im=Image.new('RGBA',(320,240))
for t in m['tiles']:
 p=root/'projects'/t['project']/'frame_000.png';assert hashlib.sha256(p.read_bytes()).hexdigest()==t['sha256'];im.alpha_composite(Image.open(p).convert('RGBA'),tuple(t['origin']))
im.save(B/'background-assembled.png');raw=bytearray()
for r,g,b,a in im.getdata():
 assert a==255;v=((r>>3)<<11)|((g>>2)<<5)|(b>>3);raw.extend([v&255,v>>8])
data=','.join(str(v) for v in raw)
(B/'orlando_v5_background.h').write_text('/* Assembled from verified editable Pixelorama tiles. */\n#pragma once\n#include "lvgl.h"\nstatic const unsigned char orlando_v5_background_data[]={'+data+'};\nstatic const lv_image_dsc_t orlando_v5_background={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=320,.h=240,.stride=640},.data_size=sizeof(orlando_v5_background_data),.data=orlando_v5_background_data};\n')
