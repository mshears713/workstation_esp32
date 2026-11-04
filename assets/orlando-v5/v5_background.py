"""Native coordinate batches for an Orlando sky and flat Florida garden.

No raster is generated outside Pixelorama. Four 80x120 native tiles per row.
"""
import asyncio, hashlib, json, math
from native_session import Native

def shade(y):
    if y < 32: return '#779fbd'
    if y < 64: return '#96b8ca'
    if y < 96: return '#b7cfce'
    if y < 128: return '#d9dfc8'
    if y < 145: return '#547f6d'
    if y < 175: return '#78a176'
    if y < 210: return '#91ad7a'
    return '#a7b987'

def colour(x,y):
    c=shade(y)
    # Small distant shrubs and warm highlights: no road or mountains.
    if 123 <= y < 145:
        for cx,cy,rx,ry in [(12,135,20,9),(45,138,20,8),(82,135,23,10),(121,138,20,7),(203,138,22,9),(243,135,25,10),(287,136,24,9),(319,137,18,8)]:
            if ((x-cx)/rx)**2+((y-cy)/ry)**2 < 1: c='#537d69' if y<cy else '#5c896f'
    if y>152 and (x*17+y*31)%181<2: c='#d5c987'
    return c

async def main():
    n=Native('background-v5'); records=[]
    try:
        old=await n.start()
        for row in range(2):
            for col in range(4):
                p=f'background-{col}-{row}'
                await n.call('create_project',project=p,width=80,height=120)
                coords={(x,y):colour(col*80+x,row*120+y) for y in range(120) for x in range(80)}
                await n.paint(p,0,0,coords)
                await n.call('save_project',project=p)
                await n.call('export_frame',project=p,frame=0)
                q=n.root/'projects'/p
                records.append({'project':p,'origin':[col*80,row*120], 'sha256':hashlib.sha256((q/'frame_000.png').read_bytes()).hexdigest()})
        await n.stop()
        fresh=await n.start(); assert fresh['boot_id']!=old['boot_id']
        for r in records:
            await n.call('open_project',project=r['project'])
            await n.call('export_frame',project=r['project'],frame=0)
            assert hashlib.sha256((n.root/'projects'/r['project']/'frame_000.png').read_bytes()).hexdigest()==r['sha256']
        (n.root/'manifest.json').write_text(json.dumps({'creation':old,'fresh':fresh,'tiles':records},indent=2))
        print('BACKGROUND_FRESH_REOPEN_PASS',flush=True)
    finally:
        await n.stop()

if __name__=='__main__': asyncio.run(main())
