"""Losslessly package accepted native PNG pixels; no creative image changes."""
from pathlib import Path
from PIL import Image
import hashlib, json
root=Path(__file__).resolve().parents[1]
items=[('steam','mike-icon-r0013-atlas.png','b7fe62f7ba51f69173c09eddfd7e545f9f332c4b37026f91446bc950687df21a','libfile_fe059a0e8c8c819197b685595ac50912','1JrWt99nOOW8vdhNFXi8Z9PBApI1K5jQw'),('transistor','mike-transistor-r0008-atlas.png','4a0586e11ef37c0c4c651969d89c6acaf1044bec9fbbf86f3152d3040379217a','libfile_38c123b03f3881918cdb40f021bd6ae4','1xEFpVs3bQ8c5GvBJFgs3mWu4JMbVMwAB'),('ai','mike-ai-r0007-atlas.png','db5d5917cc01707dec01874fd33af189f2075be8a1142e06e0296e1c0e4dc3f4','libfile_9e2b7969b3b08191b3c85a6fc5b8cb38','1R8AKoh2NeclR86K1rHnGBe_2lf5j6Fyb')]
lines=['/* Generated losslessly from accepted native Pixelorama exports. */','#pragma once','#include "lvgl.h"']
receipt=[]
for name,file,sha,lib,drive in items:
    p=root/'assets/mouse-stations'/file
    assert hashlib.sha256(p.read_bytes()).hexdigest()==sha
    im=Image.open(p).convert('RGBA');assert im.size==(32,32)
    rgba=im.tobytes();data=im.tobytes('raw','BGRA')
    assert Image.frombytes('RGBA',(32,32),data,'raw','BGRA').tobytes()==rgba
    assert set(im.getchannel('A').get_flattened_data())=={0,255}
    lines.append('static const uint8_t mouse_station_'+name+'_data[] __attribute__((aligned(4))) = {')
    lines += [','.join(str(v) for v in data[i:i+32])+',' for i in range(0,len(data),32)]
    lines += ['};','static const lv_image_dsc_t mouse_station_'+name+' = { .header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=32,.h=32,.stride=128}, .data_size=4096,.data=mouse_station_'+name+'_data };']
    receipt.append(dict(name=name,file=file,sha256=sha,rgba_sha256=hashlib.sha256(rgba).hexdigest(),source_drive_id=drive,associated_library_id=lib,library_version=0,consumed_source='canonical Drive atlas; Library Windows materializer failed before installation',size=[32,32],frames=1,alpha=[0,255],payload_bytes=4096))
lines.append('static const lv_image_dsc_t *const station_icons[3]={&mouse_station_steam,&mouse_station_transistor,&mouse_station_ai};')
(root/'main/mouse_station_assets.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
(root/'assets/mouse-stations/provenance.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print('Three exact PNGs packed; 12,288 flash-resident pixel bytes; decoded RGBA roundtrip exact.')
