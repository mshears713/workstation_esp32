from pathlib import Path
from PIL import Image
import hashlib,json
B=Path(__file__).parent;r=B/'van-door-overnight';p=r/'projects/van-entry-door/frame_000.png';m=json.loads((r/'manifest.json').read_text());assert hashlib.sha256(p.read_bytes()).hexdigest()==m['png_sha256']
im=Image.open(p).convert('RGBA');assert im.size==(15,24);raw=im.tobytes('raw','BGRA')
(B/'orlando_van_door.h').write_text('#pragma once\n#include "lvgl.h"\nstatic const uint8_t van_entry_door_data[]={'+','.join(map(str,raw))+'};\nstatic const lv_image_dsc_t van_entry_door={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=15,.h=24,.stride=60},.data_size=sizeof(van_entry_door_data),.data=van_entry_door_data};\n')
