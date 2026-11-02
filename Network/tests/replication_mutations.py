"""Deliberately break safety rules in temporary copies; the suite must reject each."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve()
libraries = [build/'libopenrune_net.a'] + [build/'_deps/mbedtls-build/library'/f'lib{x}.a' for x in ('mbedtls','mbedx509','mbedcrypto')]
cases = [
    ('action_dedupe', 'if (known!=decisions.end()) { remember(known->second); continue; }', ''),
    ('stale_held_input', ' && now-current->createdMs<=InputMaxAge', ''),
    ('stale_admitted_action', 'std::erase_if(admitted,[&](const auto& a){return now-a.localTime>InputMaxAge;});', ''),
    ('explicit_install_ack', 'return {latest,assembly?', 'return {pending?pending->id:latest,assembly?'),
    ('entity_generation', 'e.ref.generation<old->ref.generation || (e.ref.generation==old->ref.generation && e.kind!=old->kind)', 'false'),
    ('fragment_conflict', '&& !std::equal(p.bytes.begin(),p.bytes.end(),a.bytes.begin()+offset)', '&& false'),
]
results = []
with tempfile.TemporaryDirectory(prefix='open-rune-replication-mutations-') as folder:
    for name, old, new in cases:
        source = (root/'src/Replication.cpp').read_text()
        assert old in source, name
        mutant = Path(folder)/'Replication.cpp'; mutant.write_text(source.replace(old,new))
        exe = Path(folder)/name
        subprocess.run(['clang++','-std=c++20','-O1','-pthread','-I',str(root/'include'),str(mutant),
                        str(root/'tests/ReplicationTests.cpp'),*(str(p) for p in libraries),'-o',str(exe)],check=True,capture_output=True)
        run = subprocess.run([str(exe),'unit'],capture_output=True,text=True,timeout=30)
        assert run.returncode==1 and 'FAIL ' in run.stderr, (name,run.stdout,run.stderr)
        results.append({'mutation':name,'detected':True,'failure':run.stderr.strip()})
print(json.dumps(results,indent=2))
