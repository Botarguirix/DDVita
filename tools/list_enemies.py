"""Debug helper: table of enemy configs with their attack configs, from gamedata.json.

Usage: python -E -P list_enemies.py <gamedata.json>
"""
import json, sys
g = json.load(open(sys.argv[1], encoding='utf-8'))
o = g['objects']
for v in o.values():
    if v['_class'] != 'EnemyConfig':
        continue
    ref = v.get('attackConfig') or {}
    a = o.get(ref.get('$ref', ''), {})
    print(f"{v['_name'][:20]:20} hp{v['maximumHealth']:>3} kr{v['knockbackResistance']} mv{v['moveBehaviour']} rng{v['moveRange']}"
          f" gk{v['goalie']} bc{v['ballCatcher']} sbg{v['stayBehindGoal']} hl{v['healthLossPerTurn']} boss{v['boss']} |"
          f" {a.get('_name', str(ref))[:28]:28} b{a.get('attackBehaviour')} d{a.get('attackDamage')} r{(a.get('range') or {}).get('value')}"
          f" s{a.get('stunDuration')} k{a.get('knockbackDistance')} expl{a.get('explodeOnDeath')} som{a.get('spawnOnMove')}"
          f" pierce{a.get('pierce')} wind{a.get('wind')} kids{a.get('maxLiveChildren')} spawn={(a.get('enemyToSpawn') or {}).get('$ref')}")
