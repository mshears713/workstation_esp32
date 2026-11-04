from pathlib import Path
import json
p=Path(__file__).parent/'targets.json';o=json.loads(p.read_text());t=o['targets']['orlando_overnight'];t['fields']['idle_test']={'type':'bool','default':False};t['fixtures']['idle_test']={'idle_test':True};p.write_text(json.dumps(o,indent=2))
