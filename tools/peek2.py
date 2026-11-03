"""Debug helper: print one example object per class (fields truncated) from gamedata.json.

Usage: python -E -P peek2.py <gamedata.json> Class [Class ...]
"""
import json, sys
g = json.load(open(sys.argv[1], encoding='utf-8'))
o = g['objects']
for cls in sys.argv[2:]:
    items = [v for v in o.values() if v['_class'] == cls]
    print(f'== {cls} ({len(items)})')
    if items:
        d = {k: v for k, v in items[0].items() if not k.startswith('localized')}
        print(json.dumps(d)[:2500])
