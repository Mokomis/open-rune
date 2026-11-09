"""Package Open Rune for Android using an installed SDK and JDK."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import zipfile
import hashlib
import json
import re
from package_licenses import stage_licenses

p = argparse.ArgumentParser()
p.add_argument('--sdk', type=Path, required=True)
p.add_argument('--java-home', type=Path, required=True)
p.add_argument('--sdl-source', type=Path, required=True)
p.add_argument('--build-dir', type=Path, required=True)
p.add_argument('--strip', type=Path)
p.add_argument('--unsigned', action='store_true', help='Produce an aligned APK for signing on a separate machine')
p.add_argument('--keystore', type=Path, help='Existing Android signing key; never created or replaced')
p.add_argument('--discovery-runtime', type=Path, help='Prepared Bare Kit and HyperDHT Android bundle')
p.add_argument('--validation-layer', type=Path, help='Optional arm64 Khronos validation library for development checks')
p.add_argument('--turnip-driver', type=Path, help='Experimental app-local libvulkan_freedreno.so (requires AdrenoTools build)')
args = p.parse_args()
if args.unsigned and args.keystore:
    p.error('--unsigned and --keystore are mutually exclusive')
if args.keystore and not args.keystore.is_file():
    p.error('--keystore must name an existing signing key')
root = Path(__file__).resolve().parent.parent
build = args.build_dir.resolve()
out = build / 'apk'
out.mkdir(exist_ok=True)
tools = args.sdk / 'build-tools/36.0.0'
android = args.sdk / 'platforms/android-35/android.jar'
env = dict(os.environ, JAVA_HOME=str(args.java_home))

def run(command):
    command = [str(x) for x in command]
    if os.name == 'nt' and not Path(command[0]).is_file():
        for suffix in ('.exe', '.bat'):
            if Path(command[0] + suffix).is_file():
                command[0] += suffix
                break
    # D8's Windows batch wrapper has a much shorter command-line limit than
    # CreateProcess. Java and D8 both support response files for class lists.
    tool_name = Path(command[0]).stem.lower()
    if os.name == 'nt' and (tool_name == 'd8' or
                            (tool_name == 'javac' and sum(map(len, command)) > 24000)):
        response = out / (tool_name + '-arguments.txt')
        # D8 treats each line literally; javac uses quoted/escaped tokens.
        arguments = command[1:] if tool_name == 'd8' else [
            '"' + x.replace('\\', '\\\\').replace('"', '\\"') + '"' for x in command[1:]]
        response.write_text('\n'.join(arguments), encoding='utf-8')
        command = [command[0], '@' + str(response)]
    subprocess.run(command, env=env, check=True)

# The game window loads its logo from the engine archive, not Android resources.
# Reject stale archives before installing an APK that cannot open its window.
mark = (root / 'Android/res/drawable-nodpi/open_rune_mark.png').read_bytes()
with zipfile.ZipFile(build / 'SurrealEngine.pk3') as engine_assets:
    if engine_assets.read('open-rune-mark.png') != mark:
        raise RuntimeError('Rebuild engine resources: Open Rune logo is missing or stale')

assets = out / 'assets'
assets.mkdir(exist_ok=True)
if (root / 'Android/assets').exists():
    shutil.copytree(root / 'Android/assets', assets, dirs_exist_ok=True)
shutil.copy2(build / 'SurrealEngine.pk3', assets / 'SurrealEngine.pk3')
discovery = args.discovery_runtime.resolve() if args.discovery_runtime else root / 'Tools/RuneDiscovery/build/android'
if not (discovery / 'discovery.bundle').is_file():
    raise RuntimeError('Prepare the app-local discovery runtime with Tools/RuneDiscovery/build_android.py first')
discovery_meta=json.loads((discovery / 'manifest.json').read_text())
native_protocol=int(re.search(r'ProtocolVersion\s*=\s*(\d+)',(root / 'Network/include/openrune/net/Protocol.h').read_text()).group(1))
if discovery_meta.get('gameProtocol') != native_protocol:
    raise RuntimeError('Rebuild discovery from this source tree: its bundle does not match the native game protocol.')
stage_licenses(root, args.sdl_source, build, assets, discovery)
shutil.copy2(discovery / 'discovery.bundle', assets / 'discovery.bundle')

classes = out / 'classes'
dex = out / 'dex'
if classes.exists():
    shutil.rmtree(classes)  # Generated classes: do not ship removed anonymous UI classes.
classes.mkdir()
dex.mkdir(exist_ok=True)
java = list((args.sdl_source / 'android-project/app/src/main/java').rglob('*.java'))
java.extend((root / 'Android').glob('*.java'))
# Native and Java clients use one release identity; no token or private config is packaged.
identity=(root / 'SurrealEngine/OpenRuneVersion.h').read_text()
def identity_field(name):
    match=re.search(r'\b'+name+r'\s*=\s*(?:"([^"]+)"|(\d+))',identity)
    if not match: raise RuntimeError('Missing release identity: '+name)
    return match.group(1) or match.group(2)
version=identity_field('Version'); build_number=int(identity_field('Build'))
if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?',version) or not 1<=build_number<=2100000000:
    raise RuntimeError('Invalid release version/build number')
generated=out/'generated-java/OpenRuneBuild.java';generated.parent.mkdir(exist_ok=True)
generated.write_text('package org.runeclassic.port; final class OpenRuneBuild {\n'
    +'static final String VERSION='+json.dumps(version)+', REPOSITORY='+json.dumps(identity_field('Repository'))+';\n'
    +'static final int BUILD='+str(build_number)+', PROTOCOL='+str(native_protocol)+', RULES='+identity_field('GameplayRulesRevision')+';\n}\n',encoding='utf-8')
java.append(generated)
run([args.java_home / 'bin/javac', '-source', '8', '-target', '8', '-classpath', str(android)+os.pathsep+str(discovery / 'android/bare-kit/classes.jar'), '-d', classes, *java])
run([tools / 'd8', '--lib', android, '--min-api', '28', '--output', dex, *classes.rglob('*.class'), discovery / 'android/bare-kit/classes.jar'])
unsigned = out / 'unsigned.apk'
resources = out / 'resources.zip'
run([tools / 'aapt2', 'compile', '--dir', root / 'Android/res', '-o', resources])
run([tools / 'aapt2', 'link', '-o', unsigned, '--manifest', root / 'Android/AndroidManifest.xml', '-I', android, '--version-code', str(build_number), '--version-name', version, '-A', assets, resources])
libraries = [build / 'libruneclassic.so', build / 'libSurrealVideo.so', build / 'SDL2/libSDL2.so', build / 'Thirdparty/openal-soft/libopenal.so']
libraries.append(build / 'libopenrune_vulkan.so')
libraries.extend(sorted((discovery / 'android/bare-kit/jni/arm64-v8a').glob('*.so')))
libraries.extend(sorted((discovery / 'addons/arm64-v8a').glob('*.so')))
if args.turnip_driver:
    if args.turnip_driver.name != 'libvulkan_freedreno.so':
        p.error('--turnip-driver must name libvulkan_freedreno.so')
    libraries.append(args.turnip_driver.resolve())
    libraries.extend(build / 'adrenotools/src/hook' / name for name in
        ('libmain_hook.so', 'libhook_impl.so', 'libfile_redirect_hook.so', 'libgsl_alloc_hook.so'))
if args.validation_layer:
    if args.validation_layer.name != 'libVkLayer_khronos_validation.so':
        p.error('--validation-layer must name libVkLayer_khronos_validation.so')
    libraries.append(args.validation_layer.resolve())
with zipfile.ZipFile(unsigned, 'a', zipfile.ZIP_STORED) as archive:
    archive.write(dex / 'classes.dex', 'classes.dex')
    for library in libraries:
        staged = out / library.name
        shutil.copy2(library, staged)
        if args.strip:
            run([args.strip, '--strip-debug', staged])
        archive.write(staged, 'lib/arm64-v8a/' + library.name)

aligned = out / 'aligned.apk'
run([tools / 'zipalign', '-P', '16', '-f', '4', unsigned, aligned])
if args.unsigned:
    apk = out / 'open-rune-unsigned.apk'
    shutil.copy2(aligned, apk)
else:
    keystore = args.keystore.resolve() if args.keystore else out / 'debug.keystore'
    if not keystore.exists():
        run([args.java_home / 'bin/keytool', '-genkeypair', '-keystore', keystore, '-storepass', 'android', '-keypass', 'android', '-alias', 'androiddebugkey', '-dname', 'CN=Open Rune Debug', '-keyalg', 'RSA', '-validity', '3650'])
    apk = out / 'open-rune.apk'
    run([tools / 'apksigner', 'sign', '--ks', keystore, '--ks-pass', 'pass:android', '--key-pass', 'pass:android', '--out', apk, aligned])
    run([tools / 'apksigner', 'verify', '--verbose', apk])
with zipfile.ZipFile(apk) as archive:
    manifest = json.loads(archive.read('assets/licenses/manifest.json'))
    for name, expected in manifest['sha256'].items():
        if hashlib.sha256(archive.read('assets/licenses/' + name)).hexdigest() != expected:
            raise RuntimeError('Packaged notice mismatch: ' + name)
release_files = [apk, out / 'notices.zip', out / 'lgpl-sources.zip']
release_manifest = {
    'fork_revision': manifest['fork_revision'],
    'signed': not args.unsigned,
    'development_validation_layer': bool(args.validation_layer),
    'experimental_turnip_sha256': hashlib.sha256(args.turnip_driver.read_bytes()).hexdigest() if args.turnip_driver else None,
    'sha256': {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in release_files},
    'shared_library_sha256': {},
}
with zipfile.ZipFile(apk) as archive:
    release_manifest['shared_library_sha256'] = {
        name: hashlib.sha256(archive.read(name)).hexdigest()
        for name in archive.namelist() if name.startswith('lib/') and name.endswith('.so')
    }
(out / 'distribution-manifest.json').write_text(json.dumps(release_manifest, indent=2) + '\n')
print(apk)
