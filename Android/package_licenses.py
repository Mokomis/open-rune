"""Stage binary notices and corresponding LGPL sources; no game data or keys."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args])


def stage_licenses(root, sdl, build, assets, discovery=None):
    root, sdl, build, assets = map(Path, (root, sdl, build, assets))
    notices = {}
    def add(name, path):
        data = path.read_bytes()  # Required missing notices fail packaging.
        if not data.strip():
            raise ValueError(f'Empty license: {path}')
        notices[name] = data

    required = {
        'NOTICE.txt': 'NOTICE.txt',
        'DISTRIBUTION.md': 'Docs/RunePort/DISTRIBUTION.md',
        'SurrealEngine.txt': 'LICENSE.md',
        'SurrealGPU.txt': 'SurrealGPU/LICENSE.md',
        'SurrealWidgets.txt': 'SurrealWidgets/LICENSE.md',
        'RuneModel.txt': 'Docs/RuneModel-THIRD-PARTY.txt',
        'OpenAL.txt': 'Thirdparty/openal-soft/COPYING',
        'OpenAL-README.txt': 'Thirdparty/openal-soft/README.md',
        'SurrealVideo-LGPL-2.1.txt': 'SurrealVideo/COPYING.LGPLv2.1',
        'SurrealVideo-README.txt': 'SurrealVideo/README.md',
        'SnapdragonGSR.txt': 'Thirdparty/sgsr/LICENSE',
        'SnapdragonGSR-changes.txt': 'Thirdparty/sgsr/README.md',
        'FidelityFX-CAS.txt': 'Thirdparty/cas/LICENSE',
        'FidelityFX-CAS-changes.txt': 'Thirdparty/cas/README.md',
        'Swappy.txt': 'Android/Swappy-LICENSE.txt',
        'NotoSans-OFL.txt': 'Resources/noto/OFL.txt',
        'NotoSansArmenian-OFL.txt': 'Resources/noto/OFL-Armenian.txt',
        'NotoSansGeorgian-OFL.txt': 'Resources/noto/OFL-Georgian.txt',
        'kissfft-COPYING.txt': 'Thirdparty/kissfft/COPYING',
        'kissfft-BSD-3-Clause.txt': 'Thirdparty/kissfft/LICENSES/BSD-3-Clause',
        'glslang.txt': 'SurrealGPU/src/glslang/LICENSE.txt',
        'picopng.txt': 'SurrealWidgets/src/core/picopng/LICENSE.txt',
        'schrift.txt': 'Thirdparty/schrift/LICENSE.txt',
        'resample.txt': 'Thirdparty/resample/LICENSE',
        'miniz.txt': 'Thirdparty/miniz/LICENSE',
        'OpenMPT.txt': 'Thirdparty/openmpt/LICENSE',
        'JoltPhysics-MIT.txt': 'Thirdparty/JoltPhysics/LICENSE',
    }
    for name, path in required.items():
        add(name, root / path)
    add('SDL2.txt', sdl / 'LICENSE.txt')
    if discovery is not None:
        runtime = Path(discovery)
        inventory = json.loads((runtime / 'manifest.json').read_text(encoding='utf-8'))
        for name, expected in inventory['sha256'].items():
            file = runtime / name
            if digest(file.read_bytes()) != expected:
                raise ValueError('Discovery runtime changed since preparation: ' + name)
        add('HyperDHT/manifest.json', runtime / 'manifest.json')
        for file in sorted((runtime / 'notices').rglob('*')):
            if file.is_file():
                add('HyperDHT/' + file.relative_to(runtime / 'notices').as_posix(), file)

    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
    adreno = re.search(r'^OPENRUNE_ADRENOTOOLS_SOURCE:PATH=(.+)$', cache, re.M)
    if adreno:
        adreno_root = Path(adreno.group(1))
        add('AdrenoTools.txt', adreno_root / 'LICENSE')
        add('LinkerNamespaceBypass.txt', adreno_root / 'lib/linkernsbypass/LICENSE')
        add('Mesa-Turnip.txt', root / 'Android/Turnip-LICENSE.txt')
    if 'OPENRUNE_ENABLE_NETWORK:BOOL=ON' in (build / 'CMakeCache.txt').read_text(encoding='utf-8'):
        add('MbedTLS.txt', root / 'Network/thirdparty-notices/MbedTLS-LICENSE.txt')
        add('MbedTLS-framework.txt', root / 'Network/thirdparty-notices/MbedTLS-framework-LICENSE.txt')
    # Preserve all separately licensed OpenMPT source components, even optional ones.
    for path in sorted((root / 'Thirdparty/openmpt').rglob('*')):
        if path.is_file() and path.name.lower().startswith(('license', 'copying', 'authors', 'notice')):
            add('OpenMPT/' + path.relative_to(root / 'Thirdparty/openmpt').as_posix(), path)
    for name, path in [('PFFFT.txt', 'Thirdparty/resample/pffft.cpp'),
                       ('TinySHA1.txt', 'Thirdparty/TinySHA1/TinySHA1.hpp')]:
        text = (root / path).read_text(encoding='utf-8')
        notices[name] = (text[text.index('/*'):text.index('*/') + 2] + '\n').encode()
    for name in ('dr_flac', 'dr_wav', 'stb_vorbis'):
        text = (root / f'Thirdparty/{name}.h').read_text(encoding='utf-8')
        start = text.rfind('ALTERNATIVE ')
        # Keep the entire final dual-license comment, not just one license choice.
        start = text.rfind('/*', 0, start)
        if start < 0:
            raise ValueError(f'License block missing: {name}')
        end = text.index('*/', start) + 2
        notices[name + '.txt'] = (text[start:end] + '\n').encode()

    # Only tracked files inside the two LGPL library trees are exported. Never
    # archive the workspace, private game data, saves, credentials, or build tree.
    sources = {}
    untracked = git(root, 'ls-files', '--others', '--exclude-standard', '--',
                    'SurrealVideo', 'Thirdparty/openal-soft').decode().strip()
    if untracked:
        raise ValueError('Track new LGPL library files before packaging: ' + untracked)
    paths = git(root, 'ls-files', '-z', '--', 'SurrealVideo', 'Thirdparty/openal-soft').decode().split('\0')
    for name in filter(None, paths):
        path = root / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'Unsupported source entry: {name}')
        sources[name] = path.read_bytes()
    cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
    video = re.search(r'set\(SURREALVIDEO_SOURCES\s+(.*?)\n\)', cmake, re.S)
    if not video:
        raise ValueError('Cannot identify SurrealVideo build sources')
    sources['CMakeLists.txt'] = ('''cmake_minimum_required(VERSION 3.15)
project(OpenRuneLGPLSources LANGUAGES C CXX)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/openal-soft)
add_library(SurrealVideo SHARED
''' + video.group(1) + '''
)
target_include_directories(SurrealVideo PRIVATE SurrealVideo)
set_target_properties(SurrealVideo PROPERTIES CXX_STANDARD 20 C_STANDARD 17)
''').encode()
    sources['README.md'] = notices['DISTRIBUTION.md']
    sources['NOTICE.txt'] = notices['NOTICE.txt']
    sources['replace_lgpl.py'] = (root / 'Android/replace_lgpl.py').read_bytes()
    sources['source-manifest.json'] = (json.dumps({
        'fork_revision': git(root, 'rev-parse', 'HEAD').decode().strip(),
        'library_tree_changes': git(root, 'diff', 'HEAD', '--', 'SurrealVideo', 'Thirdparty/openal-soft').decode(),
        'sha256': {n: digest(d) for n, d in sorted(sources.items())},
    }, indent=2) + '\n').encode()
    source_zip = build / 'apk/lgpl-sources.zip'
    source_zip.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(source_zip, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(sources.items()):
            archive.writestr(name, data)
    notices['manifest.json'] = (json.dumps({
        'fork_revision': git(root, 'rev-parse', 'HEAD').decode().strip(),
        'lgpl_source_archive': source_zip.name,
        'lgpl_source_sha256': digest(source_zip.read_bytes()),
        'sha256': {n: digest(d) for n, d in sorted(notices.items())},
    }, indent=2) + '\n').encode()
    dest = assets / 'licenses'
    if dest.exists():
        shutil.rmtree(dest)  # Generated packaging directory only.
    for name, data in notices.items():
        path = dest / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    with zipfile.ZipFile(build / 'apk/notices.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(notices.items()):
            archive.writestr(name, data)
    return source_zip
