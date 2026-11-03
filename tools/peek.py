"""Debug helper: print prefab components and selected objects from gamedata.json.

Usage: python -E -P peek.py <gamedata.json> [SingletonOrClass ...]
"""
import json, sys
g = json.load(open(sys.argv[1], encoding='utf-8'))
o = g['objects']; s = g['singletons']
def show(r, n=700):
    if isinstance(r, dict) and '$ref' in r:
        return json.dumps(o[r['$ref']])[:n]
    return str(r)
for n in ['Player', 'Ball', 'Enemy', 'BreakableWall', 'FloorSpikes', 'LightningHazard', 'Treasure']:
    d = o[s[n]]
    print('==', n, {k: v for k, v in d.items() if (isinstance(v, dict) and ('$ref' in v or '$missing' in v)) or k in ('obstructPlayer', 'obstructEnemy', 'splatBloodOnTile')})
d = o[s['Player']]
for k in ['defaultAttack', 'pierceAttack', 'chargeAttack', 'configStep', 'configAttack', 'configWait', 'configThrow', 'configPass', 'configSwitchControl', 'configTake']:
    print(k, show(d.get(k)))
print('ballatk', show(o[s['Ball']]['attackConfig']))
print('lightatk', show(o[s['LightningHazard']]['attackConfig']))
print('spikes', show(o[s['FloorSpikes']]['hazardConfig']))
print('wall', show(o[s['BreakableWall']]['config']))
for extra in sys.argv[2:]:
    print(extra, json.dumps(o[s[extra]] if extra in s else [v for v in o.values() if v['_class'] == extra][:2])[:3000])
