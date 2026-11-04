from pathlib import Path
from PIL import Image
im=Image.open('v5-crest-grounded.png').convert('RGBA');assert im.size==(42,70)
pix=[(a<<24)|(r<<16)|(g<<8)|b for r,g,b,a in im.getdata()]
out=['/* Derived from native Pixelorama launch-crest. */','#pragma once','#include "lvgl.h"','static const uint32_t orlando_crest_data[]={']
out.extend(','.join('0x%08x'%v for v in pix[i:i+12])+',' for i in range(0,len(pix),12))
out.append('};\nstatic const lv_image_dsc_t orlando_crest={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=42,.h=70,.stride=168},.data_size=sizeof(orlando_crest_data),.data=(const uint8_t*)orlando_crest_data};')
Path('orlando_v5_crest.h').write_text('\n'.join(out)+'\n')
