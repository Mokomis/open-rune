"""Regenerate the checked-in Crest kernels. Needs glslangValidator and spirv-val."""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--compiler',default='glslangValidator');p.add_argument('--validator',default='spirv-val');args=p.parse_args();root=Path(__file__).resolve().parent
cases=[('SpectrumInit','Spectrum.hlsl','SpectrumInitalize',[]),('SpectrumUpdate','Spectrum.hlsl','SpectrumUpdate',[])]
for final,channel in [(0,'x'),(1,'y')]:cases.append(('FFTY'if final else'FFTX','FFT.hlsl','ComputeFFT',['-DSIZE=128','-DPASSES=7',f'-DCHANNEL={channel}',f'-DTX={1 if final else 128}',f'-DTY={128 if final else 1}',f'-DFINAL={final}']))
header='#pragma once\n// Generated from MIT Crest 4.23.0; see LICENSE and UPSTREAM.txt.\n#include <cstdint>\nnamespace CrestShaders {\n';rows=[]
with tempfile.TemporaryDirectory() as tmp:
 for name,file,entry,defines in cases:
  out=Path(tmp)/(name+'.spv');subprocess.run([args.compiler,'-D','-V','--target-env','vulkan1.1','--hlsl-iomap','-S','comp','-e','main','--source-entrypoint',entry,*defines,'-o',str(out),str(root/file)],check=True)
  subprocess.run([args.validator,'--target-env','vulkan1.1',str(out)],check=True);data=out.read_bytes();nums=struct.unpack('<'+'I'*(len(data)//4),data);header+=f'inline const uint32_t {name}[]={{\n'+','.join(hex(x)for x in nums)+'\n};\n';rows.append({'name':name,'sha256':hashlib.sha256(data).hexdigest(),'source':file,'entry':entry,'defines':defines})
header+='}\n';(root/'CrestShaders.h').write_text(header);(root/'shader-build.json').write_text(json.dumps(rows,indent=2))
