"""Package Windows notices and matching LGPL sources, without game data."""
import ast,hashlib,json,re,subprocess,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1];build=root/'build-windows';dest=build/'licenses';dest.mkdir(exist_ok=True)
# Share the maintained component inventory with the Android packager.
tree=ast.parse((root/'Android/package_licenses.py').read_text(encoding='utf-8'))
required=next(ast.literal_eval(n.value) for n in ast.walk(tree) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='required' for t in n.targets))
required.pop('Swappy.txt',None)
required.update({'MbedTLS.txt':'Network/thirdparty-notices/MbedTLS-LICENSE.txt','MbedTLS-framework.txt':'Network/thirdparty-notices/MbedTLS-framework-LICENSE.txt'})
notices={name:(root/path).read_bytes() for name,path in required.items()}
for folder,base in [('Thirdparty/openmpt','OpenMPT'),('Tools/RuneDiscovery/node_modules','HyperDHT')]:
 for path in (root/folder).rglob('*'):
  if path.is_file() and path.name.lower().startswith(('license','copying','authors','notice')):notices[base+'/'+path.relative_to(root/folder).as_posix()]=path.read_bytes()
for name,path in [('PFFFT.txt','Thirdparty/resample/pffft.cpp'),('TinySHA1.txt','Thirdparty/TinySHA1/TinySHA1.hpp')]:
 text=(root/path).read_text(encoding='utf-8');notices[name]=(text[text.index('/*'):text.index('*/')+2]+'\n').encode()
for name in ['dr_flac','dr_wav','stb_vorbis']:
 text=(root/f'Thirdparty/{name}.h').read_text(encoding='utf-8');start=text.rfind('/*',0,text.rfind('ALTERNATIVE '));assert start>=0;notices[name+'.txt']=text[start:text.index('*/',start)+2].encode()
notices['WINDOWS-README.txt']=b'''Open Rune Windows build\nSee NOTICE.txt and the component licenses. Node.js is a separately installed\nruntime, not included in this application bundle. HyperDHT runtime dependencies\nare included under discovery/node_modules with their original licenses.\nMatching LGPL source is in ../lgpl-sources.zip. To rebuild the libraries, unzip\nit and run cmake -S . -B build -A x64, then cmake --build build --config Release.\nWith Open Rune stopped, replace the matching OpenAL32.dll and SurrealVideo.dll\nbeside SurrealEngine.exe with your rebuilt libraries, preserving the originals.\nA Windows C++ toolchain and CMake are required. You may modify these libraries\nand reverse engineer the application to debug those modifications as permitted\nby their licenses. No original Rune game files are included in this archive.\n'''
notices['HyperDHT/package-lock.json']=(root/'Tools/RuneDiscovery/package-lock.json').read_bytes()
def git(*args):return subprocess.check_output(['git','-C',str(root),*args])
paths=git('ls-files','-z','--','SurrealVideo','Thirdparty/openal-soft').decode().split('\0')
assert paths and paths[0]
sources={n:(root/n).read_bytes() for n in paths if n and (root/n).is_file()}
cmake=(root/'CMakeLists.txt').read_text(encoding='utf-8');video=re.search(r'set\(SURREALVIDEO_SOURCES\s+(.*?)\n\)',cmake,re.S);assert video
sources['CMakeLists.txt']=('cmake_minimum_required(VERSION 3.15)\nproject(OpenRuneLGPLSources LANGUAGES C CXX)\nset(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)\nset(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)\nadd_subdirectory(Thirdparty/openal-soft)\nadd_library(SurrealVideo SHARED\n'+video.group(1)+'\n)\ntarget_include_directories(SurrealVideo PRIVATE SurrealVideo)\nset_target_properties(SurrealVideo PROPERTIES CXX_STANDARD 20 C_STANDARD 17)\n').encode()
sources['README.txt']=notices['WINDOWS-README.txt'];sources['NOTICE.txt']=notices['NOTICE.txt']
sha=lambda b:hashlib.sha256(b).hexdigest()
sources['source-manifest.json']=json.dumps({'revision':git('rev-parse','HEAD').decode().strip(),'sha256':{k:sha(v) for k,v in sources.items()}},indent=2).encode()
with zipfile.ZipFile(build/'lgpl-sources.zip','w',zipfile.ZIP_DEFLATED) as z:
 for name,data in sources.items():z.writestr(name,data)
notices['manifest.json']=json.dumps({'source_archive_sha256':sha((build/'lgpl-sources.zip').read_bytes()),'sha256':{k:sha(v) for k,v in notices.items()}},indent=2).encode()
for name,data in notices.items():
 assert data.strip(),name
 p=dest/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
print(f'Packaged {len(notices)} notices and {len(sources)} LGPL source files.')
