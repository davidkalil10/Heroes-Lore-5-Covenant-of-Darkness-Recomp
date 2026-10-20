#!/usr/bin/env python3
"""Check installers and packaging using disposable files, never real player data."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]

def main():
    reports=ROOT/'build/services-verification'
    reports.mkdir(parents=True,exist_ok=True)
    checks=[]
    with tempfile.TemporaryDirectory(prefix='hl5-installer-') as tmp:
        base=Path(tmp).resolve()
        assert base.parent==Path(tempfile.gettempdir()).resolve()
        if os.name=='nt':
            installer=ROOT/'assets/update_windows.ps1'
            for scenario in ('success','traversal','rollback'):
                app=base/scenario/'app'
                app.mkdir(parents=True)
                (app/'hl5.exe').write_bytes(b'previous-executable')
                (app/'data/internal').mkdir(parents=True)
                (app/'data/internal/kickass').write_bytes(b'player-progress')
                (app/'data/cloud_auth.json').write_bytes(b'private-token')
                package=base/scenario/'package.zip'
                with zipfile.ZipFile(package,'w') as z:
                    z.writestr('hl5.exe',b'updated-executable')
                    z.writestr('data/internal/kickass',b'PACKAGED-SAVE-MUST-BE-IGNORED')
                    z.writestr('data/cloud_auth.json',b'PACKAGED-TOKEN-MUST-BE-IGNORED')
                    z.writestr('assets/ui_font.ttf',b'updated-font')
                    if scenario=='traversal':z.writestr('../escaped-file',b'no')
                if scenario=='rollback':(app/'assets').write_bytes(b'block-directory-creation')
                result=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass',
                    '-File',str(installer),'-GamePid','2147483647','-Package',str(package),
                    '-Executable',str(app/'hl5.exe'),'-DataRoot',str(app/'data'),'-NoRestart'],
                    stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30)
                expected=0 if scenario=='success' else 1
                if result.returncode!=expected:
                    raise RuntimeError(f'{scenario}: installer returned {result.returncode}\n{result.stdout}')
                expected_exe=b'updated-executable' if scenario=='success' else b'previous-executable'
                assert (app/'hl5.exe').read_bytes()==expected_exe,scenario
                assert (app/'data/internal/kickass').read_bytes()==b'player-progress',scenario
                assert (app/'data/cloud_auth.json').read_bytes()==b'private-token',scenario
                assert not (base/scenario/'escaped-file').exists(),scenario
                checks.append('windows_installer_'+scenario)

        fixture=base/'packaging'
        (fixture/'assets').mkdir(parents=True)
        (fixture/'assets/ui_font.ttf').write_bytes(b'font')
        (fixture/'assets/cacert.pem').write_bytes(b'cert')
        (fixture/'data/internal').mkdir(parents=True)
        (fixture/'data/external').mkdir()
        (fixture/'data/internal/kickass').write_bytes(b'private-save')
        (fixture/'data/cloud_auth.json').write_bytes(b'private-token')
        (fixture/'data/services.json').write_bytes(b'private-settings')
        (fixture/'data/dict.bin').write_bytes(b'public-dictionary')
        binary=fixture/'fake-executable'
        binary.write_bytes(b'fixture')
        previous=Path.cwd()
        try:
            os.chdir(fixture)
            for tool,function,destination in (
                ('prepare_switch_romfs','prepare_romfs',fixture/'romfs'),
                ('prepare_android_assets','prepare_android_assets',fixture/'android/app/src/main/assets'),
                ('prepare_appimage','prepare_appdir',fixture/'AppDir')):
                spec=importlib.util.spec_from_file_location(tool,ROOT/f'tools/{tool}.py')
                module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
                if tool=='prepare_appimage':module.prepare_appdir(str(binary))
                else:getattr(module,function)()
                for private in ('cloud_auth.json','services.json','kickass'):
                    assert not list(destination.rglob(private)),(tool,private)
                assert (destination/'data/dict.bin').read_bytes()==b'public-dictionary'
                checks.append(tool+'_excludes_private_data')
            manifest=(fixture/'android/app/src/main/assets/assets_list.txt').read_text()
            assert 'ui_font.ttf:4:'+hashlib.sha256(b'font').hexdigest() in manifest
            checks.append('android_asset_content_hashes')
        finally:os.chdir(previous)
    (reports/'packaging.json').write_text(json.dumps({'passed':checks},indent=2)+'\n',encoding='utf-8')
    print('PASS: installer replacement/rollback and save preservation; packages exclude private data; Android asset hashes.')

if __name__=='__main__':main()
