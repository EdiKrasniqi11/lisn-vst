# LISN logo masters: every shape is a filled outline (no strokes, clips or text).
import math
INK,CREAM,PLUM='#2B1822','#FFF4EA','#5E3A4D'
STEMS=['#EF7A7F','#F4A76F','#C9A0BD','#A9B8BC']   # drums, vocals, bass, other: the app's Dusk stem colours (Theme.cpp), top strand first
f=lambda v: ('%.2f'%v).rstrip('0').rstrip('.')

def smooth(P,closed=False):
    # Catmull-Rom through P as cubic segments (no leading M)
    n=len(P); Q=P if closed else [P[0]]+P+[P[-1]]; d=''
    rng=range(n) if closed else range(1,n)
    for i in rng:
        if closed: p0,p1,p2,p3=P[i-1],P[i],P[(i+1)%n],P[(i+2)%n]
        else: p0,p1,p2,p3=Q[i-1],Q[i],Q[i+1],Q[i+2]
        d+='C%s %s %s %s %s %s'%(f(p1[0]+(p2[0]-p0[0])/6),f(p1[1]+(p2[1]-p0[1])/6),f(p2[0]-(p3[0]-p1[0])/6),f(p2[1]-(p3[1]-p1[1])/6),f(p2[0]),f(p2[1]))
    return d

def ribbon(C,W,cap0=True,cap1=True):
    # outline of a stroke along centreline C with width W[i]; round or butt ends
    N=[]
    for i in range(len(C)):
        a,b=C[max(i-1,0)],C[min(i+1,len(C)-1)]; dx,dy=b[0]-a[0],b[1]-a[1]; L=math.hypot(dx,dy)
        N.append((-dy/L,dx/L))
    Lp=[(c[0]+n[0]*w/2,c[1]+n[1]*w/2) for c,n,w in zip(C,N,W)]
    Rp=[(c[0]-n[0]*w/2,c[1]-n[1]*w/2) for c,n,w in zip(C,N,W)][::-1]
    d='M%s %s'%(f(Lp[0][0]),f(Lp[0][1]))+smooth(Lp)
    r=W[-1]/2; d+=('A%s %s 0 0 0 %s %s'%(f(r),f(r),f(Rp[0][0]),f(Rp[0][1])) if cap1 else 'L%s %s'%(f(Rp[0][0]),f(Rp[0][1])))
    d+=smooth(Rp); r=W[0]/2
    d+=('A%s %s 0 0 0 %s %s'%(f(r),f(r),f(Lp[0][0]),f(Lp[0][1])) if cap0 else '')+'Z'
    return d

def circle(cx,cy,r): return 'M%s %sA%s %s 0 1 0 %s %sA%s %s 0 1 0 %s %sZ'%(f(cx-r),f(cy),f(r),f(r),f(cx+r),f(cy),f(r),f(r),f(cx-r),f(cy))

# ---- symbol -------------------------------------------------------------
def symbol(R=90,amp=30,tail=16,stems=4,wmax=9,smax=15,mono=False):
    sine=lambda x: 128-amp*math.sin(math.pi*(x-10)/59)
    inside=lambda x: (x-128)**2+(sine(x)-128)**2<R*R
    xs=[x/20 for x in range(200,4920)]
    xa=next(x for x in xs if inside(x)); xb=max(x for x in xs if inside(x))
    pts=lambda x0,x1,n: [(x0+(x1-x0)*i/n,sine(x0+(x1-x0)*i/n)) for i in range(n+1)]
    tails=[ribbon(pts(10,xa+1.5,8),[tail]*9,True,False), ribbon(pts(xb-1.5,246,8),[tail]*9,False,True)]
    # strands: perpendicular offsets of the wave; at the edge they share the tail's width exactly
    n=36; C=pts(xa,xb,n); strands=[]
    e=tail/stems
    for k in range(stems):
        S=[];W=[]
        for i,(x,y) in enumerate(C):
            u=math.sin(math.pi*i/n)**.8                  # 0 at both edges, 1 mid-disc
            sp=e+(smax-e)*u; w=e+(wmax-e)*u
            if mono: w=e*.55+(wmax*.75-e*.55)*u           # holes need ink between them
            a,b=C[max(i-1,0)],C[min(i+1,n)]; dx,dy=b[0]-a[0],b[1]-a[1]; L=math.hypot(dx,dy)
            # offset along the wave's normal at the edges (to meet the tail square-on), straight down mid-disc
            # (a vertical shift never folds the strand over itself in the tight dips)
            nx,ny=-dy/L,dx/L; v=u**.5; nx,ny=(1-v)*nx,(1-v)*ny+v*(1 if ny>=0 else -1)*1; m=math.hypot(nx,ny); nx,ny=nx/m,ny/m
            if ny<0: nx,ny=-nx,-ny
            o=(k-(stems-1)/2)*sp; S.append((x+nx*o,y+ny*o)); W.append(w)
        strands.append(ribbon(S,W,False,False))
    return tails,strands,circle(128,128,R)

def paint(sym,disc=INK,tail=INK,cols=STEMS,disc_opacity=1):
    t,s,c=sym; op=' fill-opacity="%s"'%f(disc_opacity) if disc_opacity<1 else ''
    return '<path fill="%s"%s d="%s"/>'%(disc,op,c)+''.join('<path fill="%s" d="%s"/>'%(col,d) for col,d in zip(cols,s))+'<path fill="%s" d="%s"/>'%(tail,''.join(t))
def mono_paint(sym,col):
    t,s,c=sym
    return '<path fill="%s" fill-rule="evenodd" d="%s"/><path fill="%s" d="%s"/>'%(col,c+''.join(s),col,''.join(t))

UNBOUNDED={  # Unbounded Bold (Resources/fonts, OFL): advance and outline in font units (1000/em, y up), from fontTools
    'L':(764,'M277 750V92L184 184H745V0H70V750Z'),
    'I':(347,'M70 750H277V0H70Z'),
    'S':(843,'M36 246H247Q252 215 276.0 192.0Q300 169 340.5 156.5Q381 144 436 144Q512 144 556.0 163.5Q600 183 600 221Q600 250 575.0 266.0Q550 282 479 289L339 302Q184 316 114.0 372.5Q44 429 44 525Q44 603 89.5 657.0Q135 711 217.5 738.5Q300 766 410 766Q518 766 602.0 735.5Q686 705 735.5 649.0Q785 593 789 519H579Q575 546 553.5 565.5Q532 585 495.0 595.5Q458 606 406 606Q336 606 295.0 587.5Q254 569 254 533Q254 507 278.5 491.0Q303 475 367 469L516 454Q623 444 687.0 419.5Q751 395 780.0 352.0Q809 309 809 245Q809 166 762.0 107.5Q715 49 630.0 16.5Q545 -16 432 -16Q316 -16 227.5 16.5Q139 49 89.0 108.5Q39 168 36 246Z'),
    'N':(972,'M765 145 701 132V750H902V0H641L208 617L271 630V0H70V750H338Z'),
}

def type_lisn(x,baseline,size,tracking=.08):
    # "LISN" as the app sets it: Unbounded Bold, tracked 0.08 em, no kerning (the font has none for these pairs).
    # x is the pen origin, like CSS and JUCE text; returns one path in canvas units (y down)
    import re
    k=size/1000; pen=x; out=''
    for ch in 'LISN':
        adv,d=UNBOUNDED[ch]; cmd=None; xy=[]
        for tok in re.findall(r'[MLHVQCZ]|-?[0-9.]+',d):
            if tok.isalpha(): cmd=tok; out+=tok; continue
            v=float(tok)
            if cmd=='H': out+=f(pen+v*k)+' '; continue
            if cmd=='V': out+=f(baseline-v*k)+' '; continue
            xy.append(v)
            if len(xy)==2: out+='%s %s '%(f(pen+xy[0]*k),f(baseline-xy[1]*k)); xy=[]
        pen+=adv*k+tracking*size
    return out.replace(' Z','Z').replace(' M','M').replace(' L','L').replace(' H','H').replace(' V','V').replace(' Q','Q').strip()

H='<svg xmlns="http://www.w3.org/2000/svg" viewBox="%s"><title>LISN logo</title>%s</svg>\n'
def save(name,vb,body): open(name,'w').write(H%(vb,body))

sym=symbol(); small=symbol(R=96,amp=36,tail=26,stems=3,wmax=15,smax=19); msym=symbol(mono=True)
REV=dict(disc=PLUM,tail=CREAM)
GLASS=dict(disc=CREAM,tail=CREAM,disc_opacity=.2)   # the app's headers and the website: the disc takes on the background's colour
save('lisn-symbol.svg','0 0 256 256',paint(sym))
save('lisn-symbol-reversed.svg','0 0 256 256',paint(sym,**REV))
save('lisn-symbol-glass.svg','0 0 256 256',paint(sym,**GLASS))
save('lisn-symbol-mono.svg','0 0 256 256',mono_paint(msym,'#000'))
save('lisn-symbol-small.svg','0 0 256 256',paint(small,cols=STEMS[:3]))
save('lisn-symbol-small-reversed.svg','0 0 256 256',paint(small,PLUM,CREAM,STEMS[:3]))
# lockups: word x-height centred on the disc; gap between tail and word = one disc radius * .6
# lockups: the app's header, x8 (32 px symbol, 10 px gap, Unbounded Bold 22 px): caps centred on the disc
TS=176; tx=256+80; tb=128+750*TS/1000/2
word=lambda col: '<path fill="%s" d="%s"/>'%(col,type_lisn(tx,tb,TS))
hv='0 18 %s 206'%f(tx+(764+347+843+972-70)*TS/1000+3*.08*TS+4)
save('lisn-horizontal.svg',hv,paint(sym)+word(INK))
save('lisn-horizontal-reversed.svg',hv,paint(sym,**REV)+word(CREAM))
save('lisn-horizontal-glass.svg',hv,paint(sym,**GLASS)+word(CREAM))
save('lisn-horizontal-mono.svg',hv,mono_paint(msym,'#000')+word('#000'))
SS=78; sx=128-((764+347+843+972-140)*SS/1000+3*.08*SS)/2-70*SS/1000; sb=258+750*SS/1000
stk=lambda col: '<path fill="%s" d="%s"/>'%(col,type_lisn(sx,sb,SS))
sv='0 28 256 %s'%f(sb+6-28)
save('lisn-stacked.svg',sv,paint(sym)+stk(INK))
save('lisn-stacked-reversed.svg',sv,paint(sym,**REV)+stk(CREAM))
save('lisn-wordmark.svg','60 -10 %s 770'%f(764+347+843+972-140+3*80+20),'<path fill="%s" d="%s"/>'%(INK,type_lisn(0,750,1000)))
# app icon: ink tile, reversed small cut
tile='<rect width="256" height="256" rx="56" fill="%s"/>'%INK
save('lisn-app-icon.svg','0 0 256 256',tile+'<g transform="translate(128 128) scale(.78) translate(-128 -128)">%s</g>'%paint(small,PLUM,CREAM,STEMS[:3]))
tile2='<rect width="256" height="256" rx="56" fill="%s"/>'%CREAM
save('lisn-app-icon-light.svg','0 0 256 256',tile2+'<g transform="translate(128 128) scale(.8) translate(-128 -128)">%s</g>'%paint(small,cols=STEMS[:3]))
