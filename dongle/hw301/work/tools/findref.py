#!/usr/bin/env python3
"""Find functions referencing given VAs in an ARM PIC binary. Tracks:
  ldr rN,[pc,#k]            -> rN = ('lit', literalvalue)
  add rN,pc,rN  (lit)       -> rN = ('abs', pc+8+lit)      [pcrel base]
  add rD,rS,#imm (S abs)    -> rD = ('abs', base+imm)      [cross-reg offset]
  ldr rD,[rS,#imm] (S abs)  -> reference base+imm; rD unknown
Reset register state at each push{...,lr}. Reports func start + hit site.
Usage: findref.py <elf> <hexaddr> ...
"""
import sys,struct,re
from elftools.elf.elffile import ELFFile
from capstone import *
e=ELFFile(open(sys.argv[1],'rb'))
targets=set(int(x,16) for x in sys.argv[2:])
segs=[(s['sh_addr'],s.data()) for s in e.iter_sections() if (s['sh_flags']&2) and s['sh_type']!='SHT_NOBITS']
def rd(va,n):
    for b,d in segs:
        if b<=va<b+len(d): return d[va-b:va-b+n]
    return b''
text=e.get_section_by_name('.text'); ta=text['sh_addr']; td=text.data()
md=Cs(CS_ARCH_ARM,CS_MODE_ARM)
reg={}; func=ta; hits=[]
def note(v,addr):
    if v in targets: hits.append((func,addr,v))
for i in md.disasm(td,ta):
    mn,op=i.mnemonic,i.op_str
    if mn=='push' and 'lr' in op: func=i.address; reg={}; continue
    m=re.match(r'(\w+), \[pc, #(0x[0-9a-f]+|\d+)\]$',op)
    if mn=='ldr' and m:
        lit=i.address+8+int(m.group(2),0); v=struct.unpack('<I',rd(lit,4))[0] if rd(lit,4) else 0
        reg[m.group(1)]=('lit',v); continue
    m=re.match(r'(\w+), pc, (\w+)$',op)
    if mn=='add' and m and reg.get(m.group(2),('',0))[0]=='lit':
        val=(i.address+8+reg[m.group(2)][1])&0xffffffff; reg[m.group(1)]=('abs',val); note(val,i.address); continue
    m=re.match(r'(\w+), (\w+), #(0x[0-9a-f]+|\d+)$',op)
    if mn=='add' and m and reg.get(m.group(2),('',0))[0]=='abs':
        val=(reg[m.group(2)][1]+int(m.group(3),0))&0xffffffff; reg[m.group(1)]=('abs',val); note(val,i.address); continue
    m=re.match(r'(\w+), \[(\w+), #(0x[0-9a-f]+|\d+)\]$',op)
    if mn=='ldr' and m and reg.get(m.group(2),('',0))[0]=='abs':
        note((reg[m.group(2)][1]+int(m.group(3),0))&0xffffffff,i.address); reg[m.group(1)]=('?',0); continue
    d=re.match(r'(\w+)[,]',op)
    if d and mn not in ('cmp','cmn','tst','teq','str','strb','strh','stm','stmib','stmdb','push','b','bl','blx','bx'):
        reg.pop(d.group(1),None)
seen=set()
for f,a,t in hits:
    if (f,t) in seen: continue
    seen.add((f,t)); print("func %#010x refs %#x (at %#x)"%(f,t,a))
