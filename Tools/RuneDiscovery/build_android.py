#!/usr/bin/env python3
"""Prepare pinned, app-local ARM64 discovery runtime. Run npm ci here first.
Generated bundles, native libraries and downloaded archives stay in ignored build/.
No global install, Android settings changes, credentials or game assets.
"""
import argparse, hashlib, json, re, shutil, subprocess, urllib.request, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent
VERSION='2.5.5'
SHA256='fc68740347c8532ba49d45bf61fae9ca1f1040dc7d6c99f2f92dc30c40e84e46'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,default=ROOT/'build/android')
p.add_argument('--archive',type=Path,help='Optional cached Bare Kit v2.5.5 prebuilds.zip')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
archive=a.archive or out/'prebuilds.zip'
if not archive.exists():
    urllib.request.urlretrieve(f'https://github.com/holepunchto/bare-kit/releases/download/v{VERSION}/prebuilds.zip',archive)
if hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:raise RuntimeError('Bare Kit release checksum mismatch')
with zipfile.ZipFile(archive) as z:
    for name in ['android/bare-kit/classes.jar','android/bare-kit/jni/arm64-v8a/libbare-kit.so','android/bare-kit/jni/arm64-v8a/libc++_shared.so']:
        target=out/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(name))
for tool,options in [('bare-pack',['--linked','--out',str(out/'discovery.bundle')]),('bare-link',['--out',str(out/'addons')])]:
    subprocess.run(['node',str(ROOT/'node_modules'/tool/'bin.js'),'--host','android-arm64',*options,str(ROOT/'runtime.cjs')],check=True,cwd=ROOT)
# Symlinked external node_modules can produce bundle module IDs containing
# /../ path traversal. Bare's asset loader aborts on those IDs rather than
# presenting a recoverable Java exception. Reject the generated artifact.
raw=(out/'discovery.bundle').read_bytes().split(b'\n',1)[1].decode('utf-8')
bundle,_=json.JSONDecoder().raw_decode(raw)
module_paths=[bundle['main'],*bundle['resolutions'],*bundle['files']]
if any('..' in path.split('/') for path in module_paths):
    raise RuntimeError('Discovery bundle escapes its root. Use dependencies physically inside this source tree, not an external node_modules symlink.')
protocol=int(re.search(r'GAME_PROTOCOL\s*=\s*(\d+)',(ROOT/'protocol.cjs').read_text()).group(1))
native=int(re.search(r'ProtocolVersion\s*=\s*(\d+)',(ROOT.parents[1]/'Network/include/openrune/net/Protocol.h').read_text()).group(1))
if protocol!=native:raise RuntimeError('Discovery and native game protocol versions disagree.')
notices=out/'notices';notices.mkdir(exist_ok=True)
shutil.copytree(ROOT/'thirdparty-notices',notices/'runtime',dirs_exist_ok=True)
shutil.copy2(ROOT/'NOTICE.txt',notices/'OpenRune-Discovery.txt')
# Include the full installed package license inventory (also covers build tooling).
for package in (ROOT/'node_modules').rglob('package.json'):
    try:meta=json.loads(package.read_text())
    except (ValueError,OSError):continue
    if not meta.get('name'):continue
    for f in package.parent.iterdir():
        if f.is_file() and f.name.lower().startswith(('license','copying','notice','copyright')):
            target=notices/'npm'/(meta['name'].replace('/','_')+'-'+meta.get('version','unknown'))/f.name
            target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,target)
manifest={'gameProtocol':protocol,'bareKit':VERSION,'archiveSha256':SHA256,'hyperdht':'6.35.0','sha256':{}}
for f in [out/'discovery.bundle',*(out/'android').rglob('*.so'),*(out/'addons').rglob('*.so'),out/'android/bare-kit/classes.jar',*notices.rglob('*')]:
    if f.is_file():manifest['sha256'][f.relative_to(out).as_posix()]=hashlib.sha256(f.read_bytes()).hexdigest()
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(out)
