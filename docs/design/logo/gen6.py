# Round 6: one wave splits into four stems inside the disc and joins back up as it leaves
import math
from gen3 import H,f,INK,STEMS,word
from gen5 import path,sine,disc,tail,defs
inside=lambda x: (x-128)**2+(sine(x)-128)**2<90**2
xs=[x/10 for x in range(100,2470)]
xa=next(x for x in xs if inside(x)); xb=max(x for x in xs if inside(x))   # where the wave crosses the disc edge
def split(widths):
    sp=lambda x: 15*math.sin(math.pi*(x-xa)/(xb-xa))**.8   # zero at both edges, widest mid-disc
    s=''.join('<path d="%s" fill="none" stroke="%s" stroke-width="%s" stroke-linecap="round"/>'%(path(lambda x: sine(x)+(k-1.5)*sp(x),xa,xb,60),c,w) for k,(c,w) in enumerate(zip(STEMS,widths)))
    return disc+s+tail(10,xa)+tail(xb,246)
V={'f1':split([9,9,9,9]),'f2':split([11,9,8,7])}
for k,m in V.items():
    open(k+'-symbol.svg','w').write(H%(256,m))
    open(k+'-lockup.svg','w').write(H%(600,m+word()))
