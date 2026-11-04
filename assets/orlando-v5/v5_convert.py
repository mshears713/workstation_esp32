"""Assemble native exports into firmware RGB565; no creative drawing."""
from pathlib import Path
import json, hashlib
from PIL import Image
root=Path('background-v5')
manifest=json.loads((root/'manifest.json').read_text())
im=Image.new('RGBA',(320,240))
for tile in manifest['tiles']:
    p=root/'projects'/tile['project']/'frame_000.png'
    assert hashlib.sha256(p.read_bytes()).hexdigest()==tile['sha256']
    im.alpha_composite(Image.open(p).convert('RGBA'),tuple(tile['origin']))
im.save(root/'assembled.png')
raw=bytearray()
for r,g,b,a in im.getdata():
    assert a==255
    v=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
    raw.extend([v&255,v>>8])
header='/* Native Pixelorama tiles; see assets/orlando-v5. */\n#pragma once\n#include "lvgl.h"\nstatic const unsigned char orlando_v5_background_data[] = {\n'
header+='\n'.join(','.join(str(v) for v in raw[i:i+32])+',' for i in range(0,len(raw),32))
header+='\n};\nstatic const lv_image_dsc_t orlando_v5_background = { .header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=320,.h=240,.stride=640},.data_size=sizeof(orlando_v5_background_data),.data=orlando_v5_background_data };\n'
Path('orlando_v5_background.h').write_text(header)
print('RGB565 bytes',len(raw))
