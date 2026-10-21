#!/usr/bin/env python3
"""Keep updater, Android and Switch versions consistent. Never commits or pushes."""
import argparse
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]

def versions():
    files={p:(ROOT/p).read_text(encoding='utf-8') for p in
           ('src/service_data.h','android/app/build.gradle','Makefile.switch')}
    patterns={'src/service_data.h':r'(Version = ")([0-9.]+)(")',
              'android/app/build.gradle':r'(versionName ")([0-9.]+)(")',
              'Makefile.switch':r'(APP_VERSION\s*:=\s*)([0-9.]+)()'}
    current={p:re.search(patterns[p],s)[2] for p,s in files.items()}
    if len(set(current.values()))!=1:raise SystemExit('Version mismatch: '+str(current))
    return files,patterns,next(iter(current.values()))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version',nargs='?',help='major, minor, patch, or X.Y.Z')
    parser.add_argument('--current',action='store_true')
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--dry-run',action='store_true')
    args=parser.parse_args();files,patterns,current=versions()
    code=int(re.search(r'versionCode\s+(\d+)',files['android/app/build.gradle'])[1])
    if args.current or args.check or not args.version:
        print(f'{current} (Android versionCode {code})');return
    parts=list(map(int,current.split('.')))
    if args.version in ('major','minor','patch'):
        i=('major','minor','patch').index(args.version);parts[i]+=1;parts[i+1:]=[0]*(2-i);target='.'.join(map(str,parts))
    else:target=args.version
    if not re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)',target):raise SystemExit('Use X.Y.Z')
    if tuple(map(int,target.split('.')))<=tuple(map(int,current.split('.'))):raise SystemExit('New version must be greater than current')
    for path,source in files.items():
        updated,count=re.subn(patterns[path],lambda m:m[1]+target+m[3],source);assert count==1
        if path.endswith('build.gradle'):
            updated,count=re.subn(r'(versionCode\s+)\d+',lambda m:m[1]+str(code+1),updated);assert count==1
        if not args.dry_run:(ROOT/path).write_text(updated,encoding='utf-8')
    print(f'{current} -> {target}; Android versionCode {code} -> {code+1}'+(' (dry run)' if args.dry_run else ''))

if __name__=='__main__':main()
