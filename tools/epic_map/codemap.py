import os
import sys, bisect, pickle, collections, re
sys.path.insert(0,'.')
from match import load_asm, ADDR, RIP, DISP, SC
M=pickle.load(open(SC+'map.pkl','rb'))
def norm(t):
    t2=ADDR.sub('@',t); t2=RIP.sub('rip+@',t2)
    t2=DISP.sub(lambda m: '+@]' if 0x1000<=int(m.group(1),16)<0x2f90000 and 'rsp' not in t2 and 'rbp' not in t2 else m.group(0), t2)
    return t2
SA=load_asm(SC+'steam.asm'); EA=load_asm(SC+'epic.asm')
sidx={a:i for i,(a,_) in enumerate(SA)}; eidx={a:i for i,(a,_) in enumerate(EA)}
# anchors
anch={}
for i,j in M['pairs'].items():
    a,b=M['fs'][i],M['fe'][j]
    if a['offs']==b['offs']: anch[a['b']]=b['b']
cnt=collections.defaultdict(collections.Counter)
for (x,y),n in M['datapairs'].items():
    if x<0x3af000: cnt[x][y]+=n
for x,c in cnt.items():
    y,n=c.most_common(1)[0]
    if len(c)==1 or n>=3*sum(c.values())//4:
        anch.setdefault(x,y)
ak=sorted(anch)
SAa=[a for a,_ in SA]
def cmap(x, after=8):
    i=bisect.bisect_right(SAa,x)-1
    if i<0: return None,None
    ins=SA[i][0]
    if x-ins>=15: return None,None
    m,a=cmap_ins(ins,after)
    if m is None: return None,None
    # same instruction length
    j=eidx[m]
    if SA[i+1][0]-ins != EA[j+1][0]-m: return None,None
    return m+(x-ins), a
def cmap_ins(x, after=8):
    # try the nearest anchors below x (up to 6), walking instructions in lockstep
    k=bisect.bisect_right(ak,x)-1
    for kk in range(k, max(-1,k-6), -1):
        a=ak[kk]; b=anch[a]
        if a not in sidx or b not in eidx: continue
        i,j=sidx[a],eidx[b]; ok=True
        while i<len(SA) and SA[i][0]<x:
            if norm(SA[i][1])!=norm(EA[j][1]): ok=False; break
            i+=1; j+=1
        if not ok or i>=len(SA) or SA[i][0]!=x: continue
        # verify a few instructions after x too
        for t in range(after):
            if norm(SA[i+t][1])!=norm(EA[j+t][1]): ok=False; break
        if ok: return EA[j][0], a
    return None, None
if __name__=='__main__':
    for line in open(SC+'lits.txt'):
        v=int(line.split()[0],16)
        if v<0x3af000:
            m,a=cmap(v)
            print('%x -> %s (anchor %s) | %s'%(v, '%x'%m if m is not None else 'NONE', '%x'%a if a is not None else '-', line.split(' ',1)[1].strip()))
