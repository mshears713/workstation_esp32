from pathlib import Path
import hashlib
src=Path('/home/mshears/orlando-home-20261103/fw-overnight');build=Path('/tmp/orlando-overnight-build')
files=['main/CMakeLists.txt','main/status_deck_ui.c','main/ui_home.c','main/ui_home.h','main/ui_mouse.c','main/ui_mouse.h','main/orlando_v5_background.h','main/orlando_v5_mouse.h','main/orlando_castle_overnight.h','main/orlando_sign_overnight.h','main/orlando_princess_overnight.h','main/orlando_van_door.h']
for f in files:
 a=hashlib.sha256((src/f).read_bytes()).hexdigest();b=hashlib.sha256((build/f).read_bytes()).hexdigest();print(f,a,'MATCH' if a==b else 'DIFF')
 if a!=b:raise SystemExit(1)
print('ALL_FINAL_BUILD_INPUTS_MATCH')
