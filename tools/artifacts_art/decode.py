import re,sys,os,glob
from PIL import Image
out=sys.argv[1]
for f in sys.argv[2:]:
    t=open(f,encoding='utf-8',errors='ignore').read()
    arrays={m.group(1):m.group(2) for m in re.finditer(r'(?:static\s+)?(?:const\s+)?(?:LV_ATTRIBUTE_[A-Z_]+\s+)?uint8_t\s+(\w+)\s*\[\s*\d*\s*\]\s*=\s*\{([^}]*)\}',t)}
    for m in re.finditer(r'const\s+lv_image_dsc_t\s+(\w+)\s*=\s*\{(.*?)\};',t,re.S):
        name,body=m.group(1),m.group(2)
        w=re.search(r'\.w\s*=\s*(\d+)',body); h=re.search(r'\.h\s*=\s*(\d+)',body)
        cf=re.search(r'LV_COLOR_FORMAT_(\w+)',body); d=re.search(r'\.data\s*=\s*(?:\(const uint8_t \*\))?\s*&?(\w+)',body)
        if not(w and h and cf and d) or d.group(1) not in arrays: continue
        w,h=int(w.group(1)),int(h.group(1)); b=bytes(int(x,0) for x in arrays[d.group(1)].replace('\n','').split(',') if x.strip())
        if cf.group(1)=='ARGB8888':
            im=Image.frombytes('RGBA',(w,h),b[:w*h*4],'raw','BGRA')
        elif cf.group(1)=='RGB565':
            px=[]
            for i in range(0,w*h*2,2):
                v=b[i]|(b[i+1]<<8); px.append((((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31,255))
            im=Image.new('RGBA',(w,h)); im.putdata(px)
        else: continue
        im.save(os.path.join(out,name+'.png')); print(name,w,h,cf.group(1))
