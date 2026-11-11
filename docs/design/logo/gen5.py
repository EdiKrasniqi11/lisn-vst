# Round 5: perfect disc, Stem Strands with more character
import math
from gen3 import H,f,INK,CREAM,STEMS,word
def path(fn,x0,x1,n=40):
    # smooth open path through samples of y=fn(x) (Catmull-Rom -> cubic)
    P=[(x0+(x1-x0)*i/n,fn(x0+(x1-x0)*i/n)) for i in range(n+1)]; Q=[P[0]]+P+[P[-1]]
    d='M%s %s'%(f(P[0][0]),f(P[0][1]))
    for i in range(1,n+1):
        p0,p1,p2,p3=Q[i-1],Q[i],Q[i+1],Q[i+2]
        d+='C%s %s %s %s %s %s'%(f(p1[0]+(p2[0]-p0[0])/6),f(p1[1]+(p2[1]-p0[1])/6),f(p2[0]-(p3[0]-p1[0])/6),f(p2[1]-(p3[1]-p1[1])/6),f(p2[0]),f(p2[1]))
    return d
sine=lambda x,a=30,ph=0: 128-a*math.sin(math.pi*(x-10)/59+ph)
disc='<circle cx="128" cy="128" r="90" fill="%s"/>'%INK
def tail(x0,x1): return '<path d="%s" fill="none" stroke="%s" stroke-width="16" stroke-linecap="round"/>'%(path(sine,x0,x1,12),INK)
def strand(k,c,w,spread,x0=30,x1=226,clip='d',ph=0):
    return '<path d="%s" fill="none" stroke="%s" stroke-width="%s" stroke-linecap="round" clip-path="url(#%s)"/>'%(path(lambda x: sine(x,30-abs(k-1.5)*4,ph*(k-1.5))+(k-1.5)*spread(x),x0,x1),c,w,clip)
defs='<defs><clipPath id="d"><circle cx="128" cy="128" r="90"/></clipPath><clipPath id="o"><circle cx="128" cy="128" r="90"/><rect x="128" y="0" width="128" height="256"/></clipPath></defs>'
# 1 One in, four out: a single ink wave goes in, four stems come out and fan apart
def one_in():
    sp=lambda x: 14+max(0,x-200)*.28
    return defs+tail(10,70)+disc+''.join(strand(k,c,9,sp,30,248,'o') for k,c in enumerate(STEMS))
# 2 Unravel: the strands start bundled where the wave enters and drift apart by the exit
def unravel():
    sp=lambda x: 2+16*max(0,min(1,(x-40)/170))
    return defs+tail(10,70)+tail(186,246)+disc+''.join(strand(k,c,w,sp,ph=.12) for k,(c,w) in enumerate(zip(STEMS,[11,9,8,7])))
# 3 One in, four out, with a lit rim: a warm crescent gives the disc depth
def lit():
    rim='<path d="M38 128A90 90 0 0 1 128 38A112 112 0 0 0 38 128Z" fill="%s"/>'%STEMS[1]
    return one_in()+rim
for k,fn in [('e1',one_in),('e2',unravel),('e3',lit)]:
    open(k+'-symbol.svg','w').write(H%(256,fn()))
    open(k+'-lockup.svg','w').write(H%(600,fn()+word()))
