"""SH-2 disassembler for 0RALLY.DAT (tools only): python tools/sh2dis.py <start hex> <end hex>"""
import sys,struct
import os
D=open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','extracted','disc','files','0RALLY.DAT'),'rb').read()
BASE=0x06004000
def w(a): 
    o=a-BASE; return struct.unpack('>H',D[o:o+2])[0]
def l(a):
    o=a-BASE; return struct.unpack('>I',D[o:o+4])[0]
def s8(x): return x-256 if x&128 else x
def s12(x): return x-4096 if x&0x800 else x
def dis(a):
    o=w(a); n=(o>>8)&15; m=(o>>4)&15; d=o&15; i=o&255; h=o>>12
    R=lambda x:'r%d'%x
    if o==0x0009: return 'nop'
    if o==0x000B: return 'rts'
    if o==0x002B: return 'rte'
    if o==0x0008: return 'clrt'
    if o==0x0018: return 'sett'
    if o==0x0019: return 'div0u'
    if o==0x001B: return 'sleep'
    if o==0x0028: return 'clrmac'
    if h==0:
        if d==2:
            return 'stc %s,r%d'%({0:'sr',1:'gbr',2:'vbr'}.get(m,'?'),n)
        if d==3:
            if m==0: return 'bsrf r%d'%n
            if m==2: return 'braf r%d'%n
        if d==4: return 'mov.b r%d,@(r0,r%d)'%(m,n)
        if d==5: return 'mov.w r%d,@(r0,r%d)'%(m,n)
        if d==6: return 'mov.l r%d,@(r0,r%d)'%(m,n)
        if d==7: return 'mul.l r%d,r%d'%(m,n)
        if d==9: pass
        if d==0xA: return 'sts %s,r%d'%({0:'mach',1:'macl',2:'pr'}[m],n)
        if d==0xC: return 'mov.b @(r0,r%d),r%d'%(m,n)
        if d==0xD: return 'mov.w @(r0,r%d),r%d'%(m,n)
        if d==0xE: return 'mov.l @(r0,r%d),r%d'%(m,n)
        if d==0xF: return 'mac.l @r%d+,@r%d+'%(m,n)
        if o&0xFF==0x29: return 'movt r%d'%n
        if o&0xFF==0x0B: pass
        if o&0xFF==0x23: pass
        if o&0xF0FF==0x0029: return 'movt r%d'%n
        if o&0xF0FF==0x0023: return 'braf r%d'%n
        if o&0xF0FF==0x0003: return 'bsrf r%d'%n
    if h==1: return 'mov.l r%d,@(%d,r%d)'%(m,d*4,n)
    if h==2:
        names={0:'mov.b r%d,@r%d',1:'mov.w r%d,@r%d',2:'mov.l r%d,@r%d',4:'mov.b r%d,@-r%d',5:'mov.w r%d,@-r%d',6:'mov.l r%d,@-r%d',7:'div0s r%d,r%d',8:'tst r%d,r%d',9:'and r%d,r%d',10:'xor r%d,r%d',11:'or r%d,r%d',12:'cmp/str r%d,r%d',13:'xtrct r%d,r%d',14:'mulu.w r%d,r%d',15:'muls.w r%d,r%d'}
        return names[d]%(m,n)
    if h==3:
        names={0:'cmp/eq',2:'cmp/hs',3:'cmp/ge',4:'div1',5:'dmulu.l',6:'cmp/hi',7:'cmp/gt',8:'sub',10:'subc',11:'subv',12:'add',13:'dmuls.l',14:'addc',15:'addv'}
        return '%s r%d,r%d'%(names[d],m,n)
    if h==4:
        low=o&0xFF
        t={0x00:'shll',0x01:'shlr',0x04:'rotl',0x05:'rotr',0x08:'shll2',0x09:'shlr2',0x18:'shll8',0x19:'shlr8',0x28:'shll16',0x29:'shlr16',0x20:'shal',0x21:'shar',0x24:'rotcl',0x25:'rotcr',0x10:'dt',0x11:'cmp/pz',0x15:'cmp/pl',0x0B:'jsr @r%d',0x2B:'jmp @r%d',0x1B:'tas.b @r%d',0x0E:'ldc r%d,sr',0x1E:'ldc r%d,gbr',0x2E:'ldc r%d,vbr',0x0A:'lds r%d,mach',0x1A:'lds r%d,macl',0x2A:'lds r%d,pr',0x07:'ldc.l @r%d+,sr',0x17:'ldc.l @r%d+,gbr',0x27:'ldc.l @r%d+,vbr',0x06:'lds.l @r%d+,mach',0x16:'lds.l @r%d+,macl',0x26:'lds.l @r%d+,pr',0x03:'stc.l sr,@-r%d',0x13:'stc.l gbr,@-r%d',0x23:'stc.l vbr,@-r%d',0x02:'sts.l mach,@-r%d',0x12:'sts.l macl,@-r%d',0x22:'sts.l pr,@-r%d'}
        if low in t:
            s=t[low]
            return s%n if '%d' in s else s+' r%d'%n
        if d==0xF: return 'mac.w @r%d+,@r%d+'%(m,n)
    if h==5: return 'mov.l @(%d,r%d),r%d'%(d*4,m,n)
    if h==6:
        names={0:'mov.b @r%d,r%d',1:'mov.w @r%d,r%d',2:'mov.l @r%d,r%d',3:'mov r%d,r%d',4:'mov.b @r%d+,r%d',5:'mov.w @r%d+,r%d',6:'mov.l @r%d+,r%d',7:'not r%d,r%d',8:'swap.b r%d,r%d',9:'swap.w r%d,r%d',10:'negc r%d,r%d',11:'neg r%d,r%d',12:'extu.b r%d,r%d',13:'extu.w r%d,r%d',14:'exts.b r%d,r%d',15:'exts.w r%d,r%d'}
        return names[d]%(m,n)
    if h==7: return 'add #%d,r%d'%(s8(i),n)
    if h==8:
        sub=(o>>8)&15
        if sub==0: return 'mov.b r0,@(%d,r%d)'%(d,m)
        if sub==1: return 'mov.w r0,@(%d,r%d)'%(d*2,m)
        if sub==4: return 'mov.b @(%d,r%d),r0'%(d,m)
        if sub==5: return 'mov.w @(%d,r%d),r0'%(d*2,m)
        if sub==8: return 'cmp/eq #%d,r0'%s8(i)
        if sub==9: return 'bt 0x%x'%(a+4+s8(i)*2)
        if sub==11: return 'bf 0x%x'%(a+4+s8(i)*2)
        if sub==13: return 'bt/s 0x%x'%(a+4+s8(i)*2)
        if sub==15: return 'bf/s 0x%x'%(a+4+s8(i)*2)
    if h==9:
        t=a+4+i*2; return 'mov.w @(0x%x),r%d ; =%04x'%(t,n,w(t))
    if h==10: return 'bra 0x%x'%(a+4+s12(o&0xFFF)*2)
    if h==11: return 'bsr 0x%x'%(a+4+s12(o&0xFFF)*2)
    if h==12:
        sub=(o>>8)&15
        if sub==0: return 'mov.b r0,@(%d,gbr)'%i
        if sub==1: return 'mov.w r0,@(%d,gbr)'%(i*2)
        if sub==2: return 'mov.l r0,@(%d,gbr)'%(i*4)
        if sub==3: return 'trapa #%d'%i
        if sub==4: return 'mov.b @(%d,gbr),r0'%i
        if sub==5: return 'mov.w @(%d,gbr),r0'%(i*2)
        if sub==6: return 'mov.l @(%d,gbr),r0'%(i*4)
        if sub==7:
            t=((a+4)&~3)+i*4; return 'mova @(0x%x),r0'%t
        if sub==8: return 'tst #0x%x,r0'%i
        if sub==9: return 'and #0x%x,r0'%i
        if sub==10: return 'xor #0x%x,r0'%i
        if sub==11: return 'or #0x%x,r0'%i
    if h==13:
        t=((a+4)&~3)+i*4; return 'mov.l @(0x%x),r%d ; =%08x'%(t,n,l(t))
    if h==14: return 'mov #%d,r%d'%(s8(i),n)
    return '.word 0x%04x'%o
def dump(a0,a1):
    a=a0
    while a<a1:
        try:
            t=dis(a)
        except Exception:
            t='.word 0x%04x'%w(a)
        print('%08x  %s'%(a,t))
        a+=2
if __name__=='__main__':
    dump(int(sys.argv[1],16),int(sys.argv[2],16))
