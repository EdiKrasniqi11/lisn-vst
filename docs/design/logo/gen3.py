# Round 3: Wave Disc with texture, in the app's dusk palette
H='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d 256">%s</svg>\n'
f=lambda v: ('%.2f'%v).rstrip('0').rstrip('.')
INK,CREAM='#2B1822','#FFF4EA'
STEMS=['#EF7A7F','#F4A76F','#C9A0BD','#A9B8BC']        # vocals, drums, bass, other
LAYERS=['#7B5467','#D95F6B','#D9775F','#F08A7C','#F4A76F'] # app wave layers, back to front
def wave(y=128,a=30,x0=10,x1=246,phase=1):
    k=1.3*a*phase; d='M%s %s'%(f(x0),f(y)); q=(x1-x0)/4
    for i in range(4):
        s=k if i%2==0 else -k
        d+='C%s %s %s %s %s %s'%(f(x0+(i+.36)*q),f(y-s),f(x0+(i+.64)*q),f(y-s),f(x0+(i+1)*q),f(y))
    return d
clip='<defs><clipPath id="d"><circle cx="128" cy="128" r="90"/></clipPath></defs>'
tails='<path d="%s" fill="none" stroke="%s" stroke-width="16" stroke-linecap="round"/>'%(wave(),INK)
# 1 Stem waves: the hero wave split into four stem-coloured strands inside the disc
def strands():
    g=''.join('<path d="%s" fill="none" stroke="%s" stroke-width="9"/>'%(wave(128+(k-1.5)*14,30-abs(k-1.5)*4),c) for k,c in enumerate(STEMS))
    return clip+tails+'<circle cx="128" cy="128" r="90" fill="%s"/><g clip-path="url(#d)">%s</g>'%(INK,g)
# 2 Grooves: the disc is a record, its grooves broken by the wave
def grooves():
    rings=''.join('<circle cx="128" cy="128" r="%d" fill="none" stroke="%s" stroke-opacity=".35" stroke-width="4"/>'%(r,CREAM) for r in range(22,90,12))
    return clip+tails+'<circle cx="128" cy="128" r="90" fill="%s"/>%s<path d="%s" fill="none" stroke="%s" stroke-width="22" clip-path="url(#d)"/><path d="%s" fill="none" stroke="%s" stroke-width="12" clip-path="url(#d)"/>'%(INK,rings,wave(),INK,wave(),STEMS[0])
# 3 Wave field: the disc is filled with stacked wave layers, like the app background
def field():
    bands=''
    for k,c in enumerate(LAYERS):
        y=60+k*30; bands+='<path d="%sV256H10Z" fill="%s"/>'%(wave(y,18,10,246,1 if k%2 else -1),c)
    return clip+tails+'<circle cx="128" cy="128" r="90" fill="%s"/><g clip-path="url(#d)">%s</g><path d="%s" fill="none" stroke="%s" stroke-width="12" clip-path="url(#d)"/>'%(INK,bands,wave(),CREAM)
def word(x=290,y=28,s=.8,col=INK):
    l='M%s %sV%s'%(f(x+40*s),f(y+44*s),f(y+212*s))
    i='M%s %sV%sM%s %sV%s'%(f(x+84*s),f(y+56*s),f(y+56*s),f(x+84*s),f(y+131*s),f(y+212*s))
    cx=x+142*s; r=18.5*s
    S='M%s %sA%s %s 0 1 0 %s %sA%s %s 0 1 1 %s %s'%(f(cx+16*s),f(y+128*s),f(r),f(r),f(cx),f(y+165*s),f(r),f(r),f(cx-16*s),f(y+202*s))
    nx=x+196*s
    n='M%s %sV%sA%s %s 0 0 1 %s %sV%s'%(f(nx),f(y+212*s),f(y+152*s),f(26*s),f(26*s),f(nx+52*s),f(y+152*s),f(y+212*s))
    return '<path d="%s" fill="none" stroke="%s" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round"/>'%(l+i+S+n,col,f(22*s))
for k,fn in [('c1',strands),('c2',grooves),('c3',field)]:
    open(k+'-symbol.svg','w').write(H%(256,fn()))
    open(k+'-lockup.svg','w').write(H%(600,fn()+word()))
