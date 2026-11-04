from pathlib import Path
from PIL import Image
im=Image.open('v5-tunnel.png').convert('RGBA');assert im.size==(26,30)
pix=[(a<<24)|(r<<16)|(g<<8)|b for r,g,b,a in im.getdata()]
out=['/* Derived from native Pixelorama launch-end. */','#pragma once','#include "lvgl.h"','static const uint32_t orlando_end_data[]={']
out.extend(','.join('0x%08x'%v for v in pix[i:i+12])+',' for i in range(0,len(pix),12))
out.append('};\nstatic const lv_image_dsc_t orlando_end={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=26,.h=30,.stride=104},.data_size=sizeof(orlando_end_data),.data=(const uint8_t*)orlando_end_data};')
Path('orlando_v5_end.h').write_text('\n'.join(out)+'\n')
