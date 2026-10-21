#!/usr/bin/env python3
"""Regenerate the opcode guard for the original RGB565 menu blur (not its implementation)."""
from pathlib import Path
import argparse,re
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    asm=(ROOT/'reference/libHeroesLore5.asm').read_text(encoding='utf-8')
    ops={int(a,16)+0x100000:int(b,16) for a,b in re.findall(r'^\s*([0-9a-f]{5,8}): ([0-9a-f]{8})\s',asm,re.M)}
    path=ROOT/'src/core/menu_blur.cpp';source=path.read_text(encoding='utf-8')
    expected=',\n'.join('    0x%08xu'%ops[pc] for pc in range(0x151980,0x151c08,4))
    updated,count=re.subn(r'(?<=static constexpr u32 expected\[\]=\{\n).*?(?=\n        \};)',lambda _:expected,source,flags=re.S)
    assert count==1
    if args.check:
        if updated!=source:raise SystemExit('Menu blur opcode guard needs regeneration')
    else:path.write_text(updated,encoding='utf-8')
    print('Menu blur opcode guard matches the reference disassembly.')
if __name__=='__main__':main()
