import json,sys,collections
g=json.load(open(sys.argv[1],encoding="utf-8"))
c=collections.Counter()
def walk(v,cls,path):
    if isinstance(v,dict):
        if "$missing" in v: c[(cls,path)]+=1
        for k,x in v.items(): walk(x,cls,path+"."+k)
    elif isinstance(v,list):
        for x in v: walk(x,cls,path+"[]")
for o in g["objects"].values():
    for k,v in o.items(): walk(v,o["_class"],k)
for (cls,p),n in c.most_common(): print(n,cls,p)
