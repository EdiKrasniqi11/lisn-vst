import math
H='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d 256">%s</svg>\n'
f=lambda v: ('%.2f'%v).rstrip('0').rstrip('.')
# A: a disc sliced into four stems, each pulled sideways
def sliced(ox=0):
    r,c=104,128; out=[]; offs=[-10,12,-4,14]
    for i in range(4):
        y0=24+i*55.33; y1=y0+42
        hw=lambda y: math.sqrt(max(r*r-(y-c)**2,0))
        a,b=hw(y0),hw(y1); dx=offs[i]+ox
        out.append('M%s %sH%sA%d %d 0 0 1 %s %sH%sA%d %d 0 0 1 %s %sZ'%(
            f(c-a+dx),f(y0),f(c+a+dx),r,r,f(c+b+dx),f(y1),f(c-b+dx),r,r,f(c-a+dx),f(y0)))
    return '<path d="%s"/>'%''.join(out)
# B: an L of three nested stems, round-ended lanes
def nested(ox=0):
    w=22; p=[]
    for k in range(3):
        cx,cy=46+36*k+ox,210-36*k; rr=16
        p.append('M%d %dV%dA%d %d 0 0 0 %d %dH%d'%(cx,cy-104,cy-rr,rr,rr,cx+rr,cy,cx+104))
    return '<path d="%s" fill="none" stroke="#000" stroke-width="%d" stroke-linecap="round"/>'%(''.join(p),w)
# C: wordmark "lisn", the i is the song split into stems
def word(x=0,y=0,s=1.0):
    w=22
    T=lambda X,Y:(x+X*s,y+Y*s)
    def P(d): return d
    l='M%s %sV%s'%(f(x+40*s),f(y+44*s),f(y+212*s))
    i='M%s %sV%s'%(f(x+84*s),f(y+56*s),f(y+56*s))
    def pill(x0,y0,x1,y1,top):  # stem piece: round on one end, cut flat on the other
        r=(x1-x0)/2
        if top: return 'M%s %sV%sA%s %s 0 0 1 %s %sV%sZ'%(f(x0),f(y1),f(y0+r),f(r),f(r),f(x1),f(y0+r),f(y1))
        return 'M%s %sV%sA%s %s 0 0 1 %s %sV%sZ'%(f(x1),f(y0),f(y1-r),f(r),f(r),f(x0),f(y1-r),f(y0))
    cut='<path d="%s"/>'%(pill(x+73*s,y+120*s,x+95*s,y+164*s,1)+pill(x+81*s,y+171*s,x+103*s,y+223*s,0))
    cx=x+142*s; r=18.5*s
    S='M%s %sA%s %s 0 1 0 %s %sA%s %s 0 1 1 %s %s'%(f(cx+16*s),f(y+128*s),f(r),f(r),f(cx),f(y+165*s),f(r),f(r),f(cx-16*s),f(y+202*s))
    nx=x+196*s
    n='M%s %sV%sA%s %s 0 0 1 %s %sV%s'%(f(nx),f(y+212*s),f(y+152*s),f(26*s),f(26*s),f(nx+52*s),f(y+152*s),f(y+212*s))
    return '<path d="%s" fill="none" stroke="#000" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round"/>'%(l+i+S+n,f(w*s))+cut
open('a-symbol.svg','w').write(H%(256,sliced()))
open('b-symbol.svg','w').write(H%(256,nested()))
open('c-wordmark.svg','w').write(H%(290,word()))
# C symbol: the split i alone, centred
ci='<circle cx="121" cy="48" r="26"/><path d="M95 160V122A26 26 0 0 1 147 122V160ZM161 172V206A26 26 0 0 1 109 206V172Z"/>'
open('c-symbol.svg','w').write(H%(256,ci))
open('a-lockup.svg','w').write(H%(600,sliced()+'<g transform="translate(290 28) scale(.8)">%s</g>'%word()))
open('b-lockup.svg','w').write(H%(600,nested()+'<g transform="translate(290 28) scale(.8)">%s</g>'%word()))
