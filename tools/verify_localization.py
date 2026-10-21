#!/usr/bin/env python3
"""Real game language switches, title return and translation corpus checks; disposable saves only."""
import argparse
import collections
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--exe',required=True,type=Path);args=parser.parse_args()
    reports=ROOT/'build/localization-verification';reports.mkdir(parents=True,exist_ok=True)
    dictionary=json.loads((ROOT/'assets/locales/pt-BR.json').read_text(encoding='utf-8'))['translations']
    token=re.compile(r'%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[hlz]*[diuoxXfgesc%]')
    bad=[key for key,value in dictionary.items() if collections.Counter(token.findall(key))!=collections.Counter(token.findall(value))]
    for key,value in dictionary.items():
        if any(ord(c)>255 for c in value):raise RuntimeError('Unsupported glyphs: '+repr(key))
        if any(key.count(c)!=value.count(c) for c in ';^{}'):raise RuntimeError('Changed control commands: '+repr(key))
    if bad:raise RuntimeError('Changed printf placeholders: '+repr(bad[:5]))
    env={k:v for k,v in os.environ.items() if not k.startswith('HL5_')}
    env.update(HL5_FIXED_STEP_MS='16',SDL_AUDIODRIVER='dummy')
    keys=[part for frame in range(60,901,60) for part in ('--press-at',f'{frame}:-5')]
    for name,language,switches,frames in (
        ('english','en',[],1260),('portuguese','pt-BR',[],1260),
        ('live','en',['--language-at','1030:pt-BR','--language-at','1150:en'],1260),
        ('title','pt-BR',['--return-title-at','1300'],1500)):
        output=reports/name;output.mkdir(exist_ok=True)
        data=output/'saves';data.mkdir(exist_ok=True)
        # Re-runs must use new data paths rather than any player profile.
        import tempfile
        with tempfile.TemporaryDirectory(prefix='saves-',dir=output) as temporary:
            command=[str(args.exe.resolve()),'--headless','--language',language,'--aspect','native',
                '--data',temporary,'--frames',str(frames),'--dump',str(output/'images'),'--dump-every','120',*keys,*switches]
            with (output/'run.log').open('w',encoding='utf-8') as log:
                subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=log,check=True,timeout=120)
        log=(output/'run.log').read_text(encoding='utf-8',errors='replace')
        for forbidden in ('FAULT','bad free','exhausted','Cannot load PT-BR','Latin font=0'):
            if forbidden in log:raise RuntimeError(f'{forbidden} in {name}')
        if '\ufffd' in log:raise RuntimeError(f'Broken UTF-8 in {name}')
    def frame(name,number):return (reports/name/'images'/f'frame_{number}.bmp').read_bytes()
    if frame('english',1080)==frame('live',1080):raise RuntimeError('Live switch did not update active dialogue')
    if frame('english',1200)!=frame('live',1200):raise RuntimeError('Switching back left stale text or changed scene state')
    if frame('portuguese',1080)!=frame('live',1080):raise RuntimeError('Live Portuguese differs from fresh Portuguese')
    title=(reports/'title/images/final.bmp').read_bytes()
    if tuple(map(abs,struct.unpack_from('<ii',title,18)))!=(240,400):raise RuntimeError('Title lost native viewport')
    result={'dictionary_entries':len(dictionary),'format_tokens_preserved':True,'live_dialogue_roundtrip':True,'return_to_title_without_exit':True}
    (reports/'summary.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('PASS: dictionary format tokens; live EN/PT-BR/EN dialogue; native return to title; valid UTF-8.')

if __name__=='__main__':main()
