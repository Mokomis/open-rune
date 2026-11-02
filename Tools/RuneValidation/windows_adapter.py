"""Windows lifecycle regression: private invitations, live metadata, isolated config.
Runs only a bounded headless host and preserves its logs in a new output folder.
"""
import argparse,json,os,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True);p.add_argument('--game',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if os.name!='nt':p.error('This check requires Windows')
a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False)
config=a.output/'config';saves=a.output/'saves';config.mkdir();saves.mkdir()
invites=a.output/'invitations';states=[];result={'passed':False}
command=[str(a.binary.resolve()),'--direct',f'--save-directory={saves}',f'--rune-server={invites}','--rune-net-discovery','--rune-net-seconds=7','--rune-net-maps=DM-Hildir',str(a.game.resolve())]
with (a.output/'server.log').open('w',encoding='utf-8') as log:
 process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(config)))
 try:
  deadline=time.monotonic()+25
  while process.poll() is None and time.monotonic()<deadline:
   try:
    state=json.loads((invites/'discovery-status.json').read_text(encoding='utf-8'))
    if not states or state['updated']!=states[-1]['updated']:states.append(state)
   except FileNotFoundError:pass
   time.sleep(.1)
  assert process.wait(timeout=2)==0,'headless shutdown failed'
  assert len(states)>=3,'discovery status stopped updating'
  assert all(b['updated']>a['updated'] for a,b in zip(states,states[1:])),'status timestamp went backwards'
  # Check inherited file ACLs too, not just the parent directory.
  script="$paths=@($args[0],(Join-Path $args[0] 'player1.rune-invite'));foreach($p in $paths){$a=Get-Acl -LiteralPath $p;$ids=@($a.Access | ForEach-Object {$_.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value});$owner=([Security.Principal.WindowsIdentity]::GetCurrent()).User.Value;if(@($ids | Where-Object {$_ -ne $owner -and $_ -ne 'S-1-5-18'}).Count -or $ids.Count -lt 2){throw 'Invitation ACL exposes an unexpected principal'}};Write-Output 'owner-and-system-only'"
  # Avoid PowerShell -Command argument parsing by embedding the known local path with proper literal escaping.
  script=script.replace('$args[0]',"'"+str(invites).replace("'","''")+"'")
  acl=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',script],capture_output=True,text=True,timeout=10)
  assert acl.returncode==0 and 'owner-and-system-only' in acl.stdout,'invitation ACL check failed: '+acl.stderr
  before={p.name:p.read_bytes() for p in invites.glob('*.rune-invite')}
  with (a.output/'reuse.log').open('w',encoding='utf-8') as reuse:
   again=subprocess.run(command,stdout=reuse,stderr=subprocess.STDOUT,env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(config)),timeout=15)
  assert (config/'SurrealEngine').is_dir(),'Windows ignored isolated configuration'
  assert again.returncode==1,'existing invitation directory was not rejected cleanly'
  assert before=={p.name:p.read_bytes() for p in invites.glob('*.rune-invite')},'existing invitation was changed'
  result.update(passed=True,status_updates=len(states),private_acls=True,isolated_config=True,existing_invites_preserved=True,exit_code=process.returncode)
 except Exception as e:result['error']=str(e)
 finally:
  if process.poll() is None:process.kill();process.wait()
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result));raise SystemExit(0 if result['passed'] else 1)
