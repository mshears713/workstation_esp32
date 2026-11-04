"""Convert verified native exported atlas to six LVGL ARGB8888 frames."""
from pathlib import Path
from PIL import Image
im=Image.open('v5-mouse-refined-atlas.png').convert('RGBA')
assert im.size==(288,48)
out=['/* Derived from native Pixelorama mouse-refined-48 atlas. */','#pragma once','#include "lvgl.h"']
for f in range(6):
 pix=[]
 for y in range(48):
  for x in range(48):
   r,g,b,a=im.getpixel((f*48+x,y));pix.append((a<<24)|(r<<16)|(g<<8)|b)
 out.append('static const uint32_t orlando_mouse_%d_data[]={'%f)
 out.extend(','.join('0x%08x'%v for v in pix[i:i+12])+',' for i in range(0,len(pix),12))
 out.append('};')
 out.append('static const lv_image_dsc_t orlando_mouse_%d={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_ARGB8888,.w=48,.h=48,.stride=192},.data_size=sizeof(orlando_mouse_%d_data),.data=(const uint8_t*)orlando_mouse_%d_data};'%(f,f,f))
out.append('static const lv_image_dsc_t *orlando_mouse_frames[]={'+','.join('&orlando_mouse_%d'%i for i in range(6))+'};')
Path('orlando_v5_mouse.h').write_text('\n'.join(out)+'\n')
