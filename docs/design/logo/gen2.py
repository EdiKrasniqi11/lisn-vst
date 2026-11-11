import math
H='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d 256">%s</svg>\n'
f=lambda v: ('%.2f'%v).rstrip('0').rstrip('.')
# A: an L built as a level meter: one tall pill, a foot of short pills
def meter(ox=0):
    w,g=36,14; tops=[28,140,116,156]; r=w/2
    return ''.join('<rect x="%d" y="%d" width="%d" height="%d" rx="%s"/>'%(34+k*(w+g)+ox,t,w,228-t,f(r)) for k,t in enumerate(tops))
# B: an ear drawn as one spiral of sound, the dot is what it hears
def ear(ox=0):
    # helix: half circle over the top, then one curve down and in to the lobe; inside it, a small arc of sound
    t=lambda x: f(x+ox)
    helix='M%s 108A72 72 0 0 1 %s 108C%s 150 %s 156 %s 186A28 28 0 0 1 %s 206'%(t(60),t(204),t(204),t(170),t(166),t(112))
    inner='M%s 116A32 32 0 0 1 %s 116'%(t(100),t(164))
    return '<path d="%s" fill="none" stroke="#000" stroke-width="26" stroke-linecap="round"/>'%(helix+inner)
# C: a disc crossed by one sound wave: ink outside the disc, cut out inside it
def wavedisc(ox=0):
    c,r=128+ox,90; a=30; x0=10+ox
    seg=lambda x0,x1: 'M%s 128'%f(x0)+''.join('C%s %s %s %s %s 128'%(f(x0+(i+.36)*(x1-x0)/4),f(128-a*(1 if i%2==0 else -1)*1.3),f(x0+(i+.64)*(x1-x0)/4),f(128-a*(1 if i%2==0 else -1)*1.3),f(x0+(i+1)*(x1-x0)/4)) for i in range(4))
    wv=seg(x0,x0+236)
    return ('<defs><clipPath id="d%d"><circle cx="%d" cy="128" r="%d"/></clipPath></defs>'%(ox,c,r)+
      '<path d="%s" fill="none" stroke="#000" stroke-width="16" stroke-linecap="round"/>'%wv+
      '<circle cx="%d" cy="128" r="%d"/>'%(c,r)+
      '<path d="%s" fill="none" stroke="#fff" stroke-width="16" clip-path="url(#d%d)"/>'%(wv,ox))
def word(x=0,y=0,s=1.0):
    l='M%s %sV%s'%(f(x+40*s),f(y+44*s),f(y+212*s))
    i='M%s %sV%sM%s %sV%s'%(f(x+84*s),f(y+56*s),f(y+56*s),f(x+84*s),f(y+131*s),f(y+212*s))
    cx=x+142*s; r=18.5*s
    S='M%s %sA%s %s 0 1 0 %s %sA%s %s 0 1 1 %s %s'%(f(cx+16*s),f(y+128*s),f(r),f(r),f(cx),f(y+165*s),f(r),f(r),f(cx-16*s),f(y+202*s))
    nx=x+196*s
    n='M%s %sV%sA%s %s 0 0 1 %s %sV%s'%(f(nx),f(y+212*s),f(y+152*s),f(26*s),f(26*s),f(nx+52*s),f(y+152*s),f(y+212*s))
    return '<path d="%s" fill="none" stroke="#000" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round"/>'%(l+i+S+n,f(22*s))
W='<g transform="translate(290 28) scale(.8)">%s</g>'%word()
for k,fn in [('a',meter),('b',ear),('c',wavedisc)]:
    open(k+'-symbol.svg','w').write(H%(256,fn()))
    open(k+'-lockup.svg','w').write(H%(600,fn()+W))
