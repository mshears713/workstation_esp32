from pathlib import Path
import re,hashlib,json
B=Path('/home/mshears/orlando-home-20261103');F=B/'fw-menu3';s=(F/'main/status_deck_ui.c').read_text();names=['back_nav_cb','menu_button_cb','more_nav_button_cb','more3_nav_button_cb','weather_nav_button_cb'];parts=[]
for name in names:
 m=re.search(r'static void '+name+r'\(lv_event_t \*e\)\s*\{[^{}]*\}',s,re.S)
 assert m,name;parts.append(m.group())
start=s.index('static void render_weather(void)\n{');brace=s.index('{',start);depth=0;end=brace
for pos in range(brace,len(s)):
 if s[pos]=='{':depth+=1
 elif s[pos]=='}':
  depth-=1
  if depth==0:end=pos+1;break
mapper=s[start:end];assert 'weather_client_get(&w)' in mapper and 'ui_weather_page_apply_weather' in mapper
t=(B/'menu3_app_callbacks.template.c').read_text().replace('/* EXTRACTED_APP_CALLBACKS */','\n'.join(parts)).replace('/* EXTRACTED_APP_WEATHER_MAPPER */',mapper)
(F/'tools/uisim/targets/menu3_app_callbacks.c').write_text(t)
(B/'menu3-app-callbacks-sha256.json').write_text(json.dumps({**{n:hashlib.sha256(v.encode()).hexdigest() for n,v in zip(names,parts)},'render_weather':hashlib.sha256(mapper.encode()).hexdigest()},indent=2))
