"""Lossless packaging of PARTIAL native exports; no creative pixel changes."""
import hashlib,json,pathlib
from PIL import Image
repo=pathlib.Path(__file__).resolve().parents[1]
root=repo.parent/'lincoln-native-v2'
manifest=json.loads((root/'pack-manifest.json').read_text())
groups={'dusk-backdrop':'backdrop','log-cabin-v2':'cabin-v2','lincoln-v2':'lincoln-v2'}
code=['/* DRAFT native art: persistence verified, artist acceptance PENDING. */','#include "lincoln_art.h"']
seen={};records=[];total=0;arrays={}
for source in manifest['sources']:
    project=source['project'];folder=root/groups[project]/'projects'/project;names=[]
    assert hashlib.sha256((folder/(project+'.pxo')).read_bytes()).hexdigest()==source['pxo_sha256']
    for i in range(source['info']['frames']):
        path=folder/f'frame_{i:03}.png';digest=hashlib.sha256(path.read_bytes()).hexdigest()
        assert digest==source['frame_exports'][path.name]
        im=Image.open(path).convert('RGBA');assert im.size==(source['info']['width'],source['info']['height'])
        pixels=list(im.getdata());palette=list(dict.fromkeys(pixels));assert len(palette)<=256
        lookup={p:n for n,p in enumerate(palette)};indices=bytes(lookup[p] for p in pixels)
        assert bytes(v for index in indices for v in palette[index])==im.tobytes()
        raw=bytes(v for r,g,b,a in palette for v in (b,g,r,a))+bytes((256-len(palette))*4)+indices
        key=(im.size,hashlib.sha256(raw).hexdigest())
        if key not in seen:
            name=f'lincoln_native_{len(seen)}';seen[key]=name;total+=len(raw)
            code.append(f'static const uint8_t {name}_data[] __attribute__((aligned(4)))={{')
            code.extend(','.join(str(x) for x in raw[j:j+32])+',' for j in range(0,len(raw),32))
            code.append('};');w,h=im.size
            code.append(f'static const lv_image_dsc_t {name}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_I8,.w={w},.h={h},.stride={w}}},.data_size=sizeof({name}_data),.data={name}_data}};')
        names.append('&'+seen[key]);records.append(dict(source=str(path.relative_to(root)),sha256=digest,palette_colors=len(palette),raw_rgba_sha256=hashlib.sha256(im.tobytes()).hexdigest()))
    array=groups[project].replace('-v2','')+'_frames';arrays[project]=array
    code.append('static const lv_image_dsc_t *const '+array+'[]={'+','.join(names)+'};')
code.append('static const ui_lincoln_art_t art={.backdrop=backdrop_frames,.actor=lincoln_frames,.cabin=cabin_frames,.native_persistence_verified=true,.artist_accepted=false};')
code.append('const ui_lincoln_art_t *lincoln_art_get(void){return &art;}')
target=repo/'main/lincoln_art.c';target.write_text('\n'.join(code)+'\n')
receipt=dict(source_zip_sha256='76229262dc437e888b30afd15ee4b6ad9e439568a6c6598fb7880a99a30109dc',accepted=False,
   conversion='Exact RGBA palette/index encoding to native LVGL I8, byte-identical reconstruction verified; no resize, crop, quantization, drawing or recolouring',
   unique_payloads=len(seen),payload_bytes=total,exports=records,c_sha256=hashlib.sha256(target.read_bytes()).hexdigest())
(repo/'doc/lincoln-art-v2-receipt.json').write_text(json.dumps(receipt,indent=2))
print(json.dumps({k:v for k,v in receipt.items() if k!='exports'},indent=2))
