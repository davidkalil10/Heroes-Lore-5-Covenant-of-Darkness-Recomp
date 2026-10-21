#!/usr/bin/env python3
"""Regression captures for localized native widgets, HUD bounds and separate Settings."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def image(path):
    data=path.read_bytes();offset=struct.unpack_from('<I',data,10)[0]
    w,h=struct.unpack_from('<ii',data,18);rows=[data[offset+y*w*4:offset+(y+1)*w*4] for y in range(abs(h))]
    return w,abs(h),rows if h<0 else rows[::-1]

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--exe',required=True,type=Path);args=parser.parse_args()
    reports=ROOT/'build/ui-verification';reports.mkdir(parents=True,exist_ok=True)
    env={k:v for k,v in os.environ.items() if not k.startswith('HL5_')}
    env.update(HL5_FIXED_STEP_MS='16',HL5_TRACE_UI='1',SDL_AUDIODRIVER='dummy')
    keys=[arg for frame in range(60,901,60) for arg in ('--press-at',f'{frame}:-5')]
    def run(name,extra,language='pt-BR',interactive=False):
        output=reports/name;output.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='saves-',dir=output) as temporary:
            Path(temporary,'services.json').write_text('{"check_updates":false}')
            command=[str(args.exe.resolve()),'--language',language,'--aspect','native',
                '--data',temporary,'--frames','1500','--dump',str(output/'images'),*(keys if not interactive else ['--press-at','60:-5','--press-at','120:-5']),*extra]
            runenv=env.copy()
            if interactive:runenv.update(SDL_VIDEODRIVER='dummy',SDL_RENDER_DRIVER='software')
            else:command+=['--headless']
            with (output/'run.log').open('w',encoding='utf-8') as log:
                subprocess.run(command,cwd=ROOT,env=runenv,stdout=log,stderr=log,check=True,timeout=120)
        log=(output/'run.log').read_text(encoding='utf-8',errors='replace')
        for bad in ('FAULT','bad free','exhausted','\ufffd'):
            if bad in log:raise RuntimeError(f'{bad} in {name}')
        return log
    grid=run('grid',['--game-menu-at','1300:0'])
    for word in ('ATRIB.','BOLSA','EQUIP.','HABIL.','CARGA','ESPÍR.'):
        if 'text='+word not in grid:raise RuntimeError('Missing compact grid label: '+word)
    equip=run('equipment',['--game-menu-at','1300:0','--press-at','1340:-4','--press-at','1360:-4','--press-at','1380:-5'])
    for word in ('ATQ','DEF fís.','DEF mág.','Vel. atq.','Resist.'):
        if 'text='+word not in equip:raise RuntimeError('Missing stat label: '+word)
    skill=run('skills',['--game-menu-at','1300:0','--press-at','1340:-2','--press-at','1380:-5'])
    for word in ('TODAS','HAB.','MAGIA','ESP.','BASE'):
        if 'text='+word not in skill:raise RuntimeError('Missing tab label: '+word)
    # Outside the 30x13 native buttons the English/PT top HUD rows must be identical.
    for aspect,starts in [('native',(121,156,191)),('wide',(147,197,247))]:
        for language in ('en','pt-BR'):
            name='hud-'+aspect+'-'+language
            log=run(name,['--aspect',aspect,'--game-menu-at','1499:-1'],language)
            if language=='pt-BR' and '[ui-fit] Comprar width=28 limit=28' not in log:raise RuntimeError('Comprar did not fit HUD')
        en=image(reports/('hud-'+aspect+'-en')/'images/final.bmp')
        pt=image(reports/('hud-'+aspect+'-pt-BR')/'images/final.bmp')
        if en[:2]!=pt[:2]:raise RuntimeError('HUD changed canvas size')
        for y in range(14):
            for x in range(en[0]):
                if any(left<=x<left+30 for left in starts):continue
                if en[2][y][x*4:x*4+4]!=pt[2][y][x*4:x*4+4]:raise RuntimeError(f'HUD label spilled outside button at {aspect} {x},{y}')
    # Stop at title menu, choose its original Options tile, then leave it.
    log=run('native-options',['--frames','520','--press-at','180:-2','--press-at','200:-4',
        '--press-at','220:-5','--press-at','400:-16','--dump-every','100','--window','960x720'],interactive=True)
    if '[settings] opened' in log:raise RuntimeError('Original options opened host Settings')
    if 'text=SIM' not in log and 'text=NÃO' not in log:raise RuntimeError('Original options screen was not exercised')
    # The explicit row is available in both original Options lists. Ordinary
    # option editing/Back must remain native; only the new action opens the modal.
    base=['--frames','520','--press-at','180:-2','--press-at','200:-4','--press-at','220:-5','--window','960x720']
    opened=run('native-title-port',base+['--press-at','270:-1','--press-at','300:-5','--press-at','360:-16','--press-at','420:-5','--dump-every','20'],interactive=True)
    if opened.count('[settings] opened')!=2:raise RuntimeError('Native title row must open, close and reopen Settings')
    if 'title:pt-BR:selected' not in opened:raise RuntimeError('Title row was not selected')
    game=run('native-game-port',['--game-menu-at','1300:9','--press-at','1310:-2','--press-at','1320:-5','--press-at','1350:-1','--press-at','1380:-5','--press-at','1420:-16','--dump-every','20'])
    if '[settings] opened' not in game or 'game:pt-BR:selected' not in game:raise RuntimeError('In-game native options row failed')
    touched=run('native-title-touch',base+['--touch-at','270:80:287','--press-at','360:-16'],interactive=True)
    if touched.count('[settings] opened')!=1:raise RuntimeError('Native title touch row failed')
    (reports/'summary.json').write_text(json.dumps({'compact_grid':True,'stat_columns':True,'skill_tabs':True,
        'hud_no_spill_native_and_wide':True,'original_options_without_host_modal':True,'explicit_title_game_row':True,'native_touch_row':True,'close_reopen':True},indent=2)+'\n')
    print('PASS: compact native menus/stats/tabs; HUD text stays inside buttons; original Options stays independent; explicit title/game/touch row and close/reopen pass.')

if __name__=='__main__':main()
