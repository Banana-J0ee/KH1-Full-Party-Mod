import os
import pickle, bisect, collections
SC = os.environ.get('EPICMAP_WORK', 'work') + '/'
M=pickle.load(open(SC+'map.pkl','rb'))
code=M['code']; cb=[c[0] for c in code]
def cmap(x):
    i=bisect.bisect_right(cb,x)-1
    if i>=0 and code[i][0]<=x<code[i][1]: return x+code[i][2], code[i]
    return None, None
dd=collections.defaultdict(collections.Counter)
for (x,y),n in M['datapairs'].items():
    if x>=0x3af000: dd[x][y-x]+=n
pts=sorted((x,c.most_common(1)[0][0],sum(c.values()),len(c)) for x,c in dd.items())
# robust regions: run-length of delta, then drop runs of <=2 points sandwiched by same delta
runs=[]
for x,d,n,k in pts:
    if runs and runs[-1]['d']==d: runs[-1]['e']=x; runs[-1]['n']+=1
    else: runs.append(dict(s=x,e=x,d=d,n=1))
changed=True
while changed:
    changed=False
    for i in range(1,len(runs)-1):
        if runs[i]['n']<=2 and runs[i-1]['d']==runs[i+1]['d'] and runs[i-1]['n']>=3:
            runs[i-1]['e']=runs[i+1]['e']; runs[i-1]['n']+=runs[i]['n']+runs[i+1]['n']
            del runs[i:i+2]; changed=True; break
regions=[r for r in runs if r['n']>=3]
pickle.dump(dict(regions=regions,pts=pts,dd=dd),open(SC+'regions.pkl','wb'))
def dmap(x):
    # region containing x, or between two regions with equal delta
    best=None
    for i,r in enumerate(regions):
        if r['s']<=x<=r['e']: return x+r['d'], r
    for i in range(len(regions)-1):
        a,b=regions[i],regions[i+1]
        if a['e']<x<b['s']:
            if a['d']==b['d']: return x+a['d'], ('gap-same',a,b)
            return None, ('gap-diff',a,b)
    return None, None
if __name__=='__main__':
    print(len(regions),'regions; .data ones:')
    for r in regions:
        if r['s']>=0x4d5000: print(hex(r['s']),hex(r['e']),hex(r['d']),r['n'])
