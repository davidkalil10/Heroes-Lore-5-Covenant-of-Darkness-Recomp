#!/usr/bin/env python3
"""Compare accelerated menu blur with original ARM execution using disposable saves."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',required=True,type=Path)
    args=parser.parse_args();exe=args.exe.resolve()
    output=ROOT/'build/menu-blur-verification';output.mkdir(parents=True,exist_ok=True)
    results=[]
    for aspect in ('native','wide','auto'):
        for language in ('en','pt-BR'):
            captures=[]
            for original in (True,False):
                name=f'{aspect}-{language}-'+('original' if original else 'fast')
                dest=output/name;dest.mkdir(exist_ok=True)
                env={k:v for k,v in os.environ.items() if not k.startswith('HL5_')}
                env.update(HL5_FIXED_STEP_MS='16',SDL_AUDIODRIVER='dummy')
                if original:env['HL5_NO_MENU_BLUR']='1'
                else:env['HL5_VERIFY_MENU_BLUR']='1'
                with tempfile.TemporaryDirectory(prefix='hl5-blur-') as saves:
                    Path(saves,'services.json').write_text('{"check_updates":false}')
                    cmd=[str(exe),'--headless','--data',saves,'--language',language,'--aspect',aspect,
                         '--window','960x720','--frames','1320','--game-menu-at','1300:0','--dump',str(dest)]
                    for frame in range(60,901,60):cmd+=['--press-at',f'{frame}:-5']
                    run=subprocess.run(cmd,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
                (dest/'run.log').write_bytes(run.stdout)
                log=run.stdout.decode('utf-8',errors='replace')
                if run.returncode:raise RuntimeError(f'{name} failed: see {dest}/run.log')
                if not original and log.count('[menu-blur] differential PASS')<3:
                    raise RuntimeError('Expected top/interior/bottom full-state checks: '+name)
                captures.append((dest/'final.bmp').read_bytes())
            if captures[0]!=captures[1]:raise RuntimeError('Menu pixels changed: '+aspect+' '+language)
            results.append(dict(aspect=aspect,language=language,identical=True,full_state_rows=3))
            print('PASS',aspect,language,'identical image; 3 complete ARM/memory row comparisons',flush=True)
    (output/'summary.json').write_text(json.dumps(results,indent=2)+'\n')

if __name__=='__main__':main()
