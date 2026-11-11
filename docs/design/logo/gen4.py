# Round 4: Stem Strands on a disc that is not a perfect circle
import math
from gen3 import H,f,INK,STEMS,wave,word
def blob(rf,n=72):
    # smooth closed path through polar samples (Catmull-Rom -> cubic)
    P=[(128+rf(t)*math.cos(t),128+rf(t)*math.sin(t)) for t in [2*math.pi*i/n for i in range(n)]]
    d='M%s %s'%(f(P[0][0]),f(P[0][1]))
    for i in range(n):
        p0,p1,p2,p3=P[i-1],P[i],P[(i+1)%n],P[(i+2)%n]
        d+='C%s %s %s %s %s %s'%(f(p1[0]+(p2[0]-p0[0])/6),f(p1[1]+(p2[1]-p0[1])/6),f(p2[0]-(p3[0]-p1[0])/6),f(p2[1]-(p3[1]-p1[1])/6),f(p2[0]),f(p2[1]))
    return d+'Z'
def mark(shape):
    tails='<path d="%s" fill="none" stroke="%s" stroke-width="16" stroke-linecap="round"/>'%(wave(),INK)
    g=''.join('<path d="%s" fill="none" stroke="%s" stroke-width="9"/>'%(wave(128+(k-1.5)*14,30-abs(k-1.5)*4),c) for k,c in enumerate(STEMS))
    return '<defs><clipPath id="d"><path d="%s"/></clipPath></defs>%s<path d="%s" fill="%s"/><g clip-path="url(#d)">%s</g>'%(shape,tails,shape,INK,g)
V={
 'd1':blob(lambda t: 90*(1+.035*math.sin(9*t))),                                   # the edge vibrates
 'd2':blob(lambda t: 90*(1+.07*math.sin(t+.6)+.05*math.sin(2*t-1.1)+.025*math.sin(3*t+.4))),  # a pebble
 'd3':blob(lambda t: 90*(1+.10*math.cos(t)**8)*(1-.05*math.sin(t)**2)),            # stretched where the wave goes in and out
}
for k,s in V.items():
    open(k+'-symbol.svg','w').write(H%(256,mark(s)))
    open(k+'-lockup.svg','w').write(H%(600,mark(s)+word()))
