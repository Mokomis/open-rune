"""Android fourth-player match cycle, native resolution/4x AA, isolated saves.

Restarts Open Rune. The tablet must be unlocked and available for automation.
No preferences, campaign saves or refresh rules are changed.
"""
import argparse,json,subprocess,tempfile,time,re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('binary','game','output'):p.add_argument('--'+name,type=Path,required=True)
for name in ('device','host'):p.add_argument('--'+name,required=True)
p.add_argument('--adb',default='adb')
p.add_argument('--team',action='store_true')
p.add_argument('--arena',action='store_true')
p.add_argument('--headball',action='store_true')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
adb=[a.adb,'-s',a.device];processes=[];logs=[];fps=[]
def shell(*args):
    for attempt in range(5):
        try:return subprocess.run(adb+['shell',*args],capture_output=True,text=True,check=True,timeout=20).stdout
        except subprocess.CalledProcessError as error:
            if attempt==4:raise RuntimeError(error.stderr.strip()) from error
            time.sleep(1)
def log():return shell('run-as','org.runeclassic.port','cat','files/native-run.log')
def shot(name):
    (a.output/(name+'.png')).write_bytes(subprocess.check_output(adb+['exec-out','screencap','-p'],timeout=20))
def preservation():
    result=shell('run-as','org.runeclassic.port','sh','-c',"'sha256sum shared_prefs/MainActivity.xml files/game/Save/*'")
    return {'hashes':result,'refresh_rule':shell('getprop','persist.opdrr.42800e61').strip()}
before=preservation();(a.output/"before.json").write_text(json.dumps(before,indent=2)+"\n")
with tempfile.TemporaryDirectory(prefix='open-rune-android-match-') as folder:
    root=Path(folder);invites=root/'invitations'
    def launch(name,options):
        saves=root/(name+'-saves');saves.mkdir();f=(a.output/(name+'.log')).open('w');logs.append(f)
        proc=subprocess.Popen([str(a.binary.resolve()),'--direct',f'--save-directory={saves}',*options,str(a.game.resolve())],stdout=f,stderr=subprocess.STDOUT);processes.append(proc);return proc
    try:
        server=launch('server',[f'--rune-server={invites}',f'--rune-net-address={a.host}','--rune-net-players=4',
            '--rune-net-mode=headball' if a.headball else '--rune-net-mode=arena' if a.arena else '--rune-net-mode=tdm' if a.team else '--rune-net-mode=dm',
            '--rune-net-maps=HB-Agora' if a.headball else '--rune-net-maps=AR-Champions' if a.arena else '--rune-net-maps=DM-Team_ships' if a.team else '--rune-net-maps=DM-Hildir,DM-Thorstadt','--rune-net-time-limit=45','--rune-net-frag-limit=0',
            '--rune-net-intermission=5','--rune-net-seconds=210','--rune-net-test-headball' if a.headball else '--rune-net-test-arena-mode' if a.arena else '--rune-net-test-team' if a.team else '--rune-net-test-match'])
        deadline=time.monotonic()+20
        while not (invites/'player4.rune-invite').exists():
            if server.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Host did not start')
            time.sleep(.1)
        shell('run-as','org.runeclassic.port','mkdir','-p','files/multiplayer')
        subprocess.run(adb+['shell','run-as','org.runeclassic.port','sh','-c',"'umask 077; cat > files/multiplayer/cycle.rune-invite'"],input=(invites/('player1.rune-invite' if a.arena else 'player3.rune-invite' if a.team or a.headball else 'player4.rune-invite')).read_bytes(),check=True,timeout=20)
        for i in ((2,3,4) if a.arena else (1,2,4) if a.team or a.arena or a.headball else (1,2,3)):launch(f'client{i}',[f'--rune-join={invites/f"player{i}.rune-invite"}','--rune-net-seconds=195',
            '--maxfps=20','--rune-net-width=640','--rune-net-height=480','--render-msaa=2','--render-omit-hit=1',*(['--rune-net-test-headball-input'] if a.headball else ['--rune-net-test-arena-input'] if a.arena else ['--rune-net-test-team-input'] if a.team else [])])
        shell('am','force-stop','org.runeclassic.port')
        shell('am','start','-W','-n','org.runeclassic.port/.MainActivity','--es','saveTest','network-match-cycle',
            '--es','networkInvite','cycle.rune-invite','--ei','networkSeconds','175','--ez','upscale','false',
            '--ei','msaa','2','--ei','seed','1','--ei','profile','900')
        deadline=time.monotonic()+190;seen=set();map_started={};menu=False;latest=''
        while time.monotonic()<deadline:
            latest=log();(a.output/'android.log').write_text(latest)
            if 'OPEN_RUNE_CLIENT_STOP' in latest:break
            if server.poll() is not None:raise RuntimeError('Host stopped early')
            for r in ((1,2) if a.team or a.arena or a.headball else (1,2,3)):
                marker=f'OPEN_RUNE_CLIENT_SCORE round={r} phase=2'
                if marker in latest and (not a.headball or re.search(rf'OPEN_RUNE_CLIENT_SCORE round={r} phase=2 .* hb=1,0,1,1',latest)) and r not in seen:
                    seen.add(r);map_started[r]=time.monotonic()
                    shot(f'round{r}')
                    if r<=(1 if a.team or a.arena or a.headball else 2):
                        out=(a.output/f'fps-round{r}').resolve()
                        f=(a.output/f'fps-round{r}.log').open('w');logs.append(f)
                        proc=subprocess.Popen(['python3',str(Path(__file__).with_name('android_present.py')),'--adb',a.adb,'--device',a.device,'--samples','32','--output',str(out)],stdout=f,stderr=subprocess.STDOUT)
                        processes.append(proc);fps.append(proc)
            if 1 in map_started and not menu and time.monotonic()>map_started[1]+20:
                shell('input','keyevent','4');time.sleep(1);shot('scoreboard-menu');shell('input','keyevent','4');menu=True
            if len(seen)==(2 if a.team or a.arena or a.headball else 3):break
            time.sleep(1)
        assert seen==({1,2} if a.team or a.arena or a.headball else {1,2,3}), 'Android did not finish two map transitions'
        assert ('OPEN_RUNE_CLIENT_TRAVEL round=2 map='+('HB-Agora' if a.headball else 'AR-Champions' if a.arena else 'DM-Team_ships' if a.team else 'DM-Thorstadt')) in latest
        if not a.team and not a.arena and not a.headball:assert 'OPEN_RUNE_CLIENT_TRAVEL round=3 map=DM-Hildir' in latest
        sessions=re.findall(r'OPEN_RUNE_CLIENT_TRAVEL .* session=(\d+)',latest);assert len(set(sessions))==1
        if a.headball:
            hostlog=(a.output/'server.log').read_text();points=int(re.search(r'OPEN_RUNE_HEADBALL_GOAL points=(\d+)',hostlog)[1])
            assert f'OPEN_RUNE_CLIENT_SCORE round=1 phase=3 winner=0 team=0 p1={points},0 p2=0,1 p3=0,0 p4=0,0 hb=1,0,1,1 red={points} blue=0' in latest
            for marker in ['kind=6 accepted=1','kind=7 accepted=1','kind=9 accepted=1','OPEN_RUNE_HEADBALL_KILL no_points=1']:assert marker in hostlog
        elif a.arena:
            assert 'OPEN_RUNE_CLIENT_SCORE round=1 phase=3 winner=1' in latest
            for phase in (1,2,3):assert f'arena={phase},' in latest
            hostlog=(a.output/'server.log').read_text()
            assert 'OPEN_RUNE_ARENA_DEFEAT round=2' in hostlog and 'OPEN_RUNE_ARENA_DEFEAT round=3' in hostlog
            assert 'OPEN_RUNE_PLAYER_LIFE' in hostlog
        elif a.team:
            assert 'OPEN_RUNE_CLIENT_SCORE round=1 phase=3 winner=0 team=0 p1=-1,1 p2=0,1 p3=2,0 p4=0,1 red=1 blue=0' in latest
            assert 'OPEN_RUNE_TEAM_FRIENDLY_OK' in (a.output/'server.log').read_text()
        else:
            for r in (1,2):assert f'OPEN_RUNE_CLIENT_SCORE round={r} phase=3 winner=3 team=255 p1=-1,1 p2=0,1 p3=1,0 p4=0,0' in latest
        for proc in fps:assert proc.wait(timeout=25)==0,'No fresh display-FPS sample'
        assert '2520x1680' in latest,'Native surface dimensions not confirmed'
        print('PASS Android multiplayer: headball='+str(a.headball)+', arena='+str(a.arena)+', team='+str(a.team)+', scores, results, map restart, same connection')
    finally:
        for proc in processes:
            if proc.poll() is None:proc.terminate()
        for proc in processes:
            try:proc.wait(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()
        for f in logs:f.close()
        shell('am','force-stop','org.runeclassic.port')
        shell('run-as','org.runeclassic.port','rm','-f','files/multiplayer/cycle.rune-invite')
        after=preservation();(a.output/'preservation.json').write_text(json.dumps({'before':before,'after':after,'unchanged':before==after},indent=2)+'\n')
        assert before==after,'Unexpected save/preferences/refresh change'
        shell('am','start','-W','-n','org.runeclassic.port/.MainActivity','--es','saveTest','network-mode-title')
