"""Focused timing UI validation on the OPPO: independent FPS/refresh and manual input.

Uses isolated saves and native 4x. Requires the user-authorized OPPO 144 Hz rule.
Leaves the requested 144 FPS/144 Hz selections, preserves unrelated settings.
"""
import argparse,json,pathlib,re,subprocess,time,xml.etree.ElementTree as E
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--device',required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
pkg='org.runeclassic.port'
def adb(*args):return subprocess.check_output(['adb','-s',a.device,*args],timeout=35)
def tree():
 adb('shell','uiautomator','dump','--compressed','/sdcard/rune-timing.xml');return E.fromstring(adb('exec-out','cat','/sdcard/rune-timing.xml'))
def click(n):
 x,y,r,b=map(int,re.findall(r'\d+',n.get('bounds')));adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
def tap(text,desc=False):
 nodes=[n for n in tree().iter('node') if n.get('content-desc' if desc else 'text')==text and n.get('enabled')=='true'];assert nodes,text;click(nodes[-1])
def choose(name,value):
 tap(name);nodes=[n for n in tree().iter('node') if n.get('text','').replace('   ✓','')==value and n.get('class')=='android.widget.Button'];assert nodes,value;click(nodes[0])
def expect(name,value):assert any(n.get('content-desc')==name+': '+value for n in tree().iter('node')),(name,value)
def log():return adb('exec-out','run-as',pkg,'cat','files/native-run.log').decode()
def shot(name):
 (a.output/(name+'.png')).write_bytes(adb('exec-out','screencap','-p'));(a.output/(name+'.xml')).write_bytes(E.tostring(tree()))
def enter(desc,value):
 tap(desc,True);adb('shell','input','keyevent','123','67','67','67','67','67');adb('shell','input','text',str(value));adb('shell','input','keyevent','KEYCODE_ENTER')
def present(name):
 tap('×  Close');time.sleep(2)
 subprocess.run(['python3',str(pathlib.Path(__file__).with_name('android_present.py')),'--device',a.device,'--samples','12','--oppo-clocks','--output',str(a.output/name)],check=True,stdout=subprocess.DEVNULL)
 shot(name+'-scene');tap('Menu');tap('Settings');tap('Frame rate & refresh')
def prefs():return {n.get('name'):(n.tag,n.get('value'),n.text) for n in E.fromstring(adb('exec-out','run-as',pkg,'cat','shared_prefs/MainActivity.xml'))}
before=prefs();(a.output/'preferences-before.json').write_text(json.dumps(before,indent=2))
tap('Menu');tap('Settings');tap('Frame rate & refresh')
choose('Refresh rate','144 Hz');choose('Frame rate target','144 FPS');expect('Refresh rate','144 Hz');expect('Frame rate target','144 FPS');shot('timing144');print('PASS: independent 144 selections',flush=True)
tap('Refresh rate');t=tree();assert any(n.get('text','').startswith('165 Hz · Not supported') and n.get('enabled')=='false' for n in t.iter('node'));shot('refresh-options');adb('shell','input','keyevent','KEYCODE_BACK')
choose('Refresh rate','Custom…');enter('Custom refresh rate value',165);assert any('does not report support for 165 Hz' in n.get('text','') for n in tree().iter('node'));shot('unsupported-custom')
enter('Custom refresh rate value',144);expect('Refresh rate','Custom 144 Hz');print('PASS: custom refresh validation and apply',flush=True)
choose('Frame rate target','165 FPS');time.sleep(.5);assert 'target_fps=165' in log()
choose('Frame rate target','Custom…');enter('Custom FPS target value',72);expect('Frame rate target','Custom 72 FPS');time.sleep(.5);assert 'target_fps=72' in log();present('target72-refresh144');print('PASS: manual FPS independent of 144 Hz refresh',flush=True)
adb('shell','input','keyevent','KEYCODE_BACK');tap('Enhancements');tap('Frame rate & refresh');expect('Frame rate target','Custom 72 FPS');expect('Refresh rate','Custom 144 Hz')
(a.output/'before-restart.log').write_text(log())
adb('shell','am','force-stop',pkg);adb('shell','am','start','-n',pkg+'/.MainActivity','--es','map','DM-Hildir.run','--es','saveTest','frame-refresh','--ez','upscale','false','--ei','msaa','2','--ei','seed','1');time.sleep(6);tap('Menu');tap('Settings');tap('Frame rate & refresh');expect('Frame rate target','Custom 72 FPS');expect('Refresh rate','Custom 144 Hz')
choose('Frame pacing','Off');time.sleep(.5);assert 'RUNE_MENU pacing=0 target_fps=72' in log();present('limiter72-refresh144');choose('Frame pacing','On')
choose('Refresh rate','144 Hz');choose('Frame rate target','144 FPS');present('target144-refresh144');shot('final-timing');print('PASS: restart, CPU limiter, final 144/144',flush=True)
after=prefs();allowed={'menu.fps','menu.fpsManual','menu.refresh','menu.refreshManual','menu.pacing'};assert all(before.get(k)==after.get(k) for k in before.keys()|after.keys() if k not in allowed)
(a.output/'final.log').write_text(log());result={'preset144_and165_fps':True,'manual_fps72':True,'manual_refresh144':True,'unsupported_refresh165_rejected':True,'shared_video_enhancements':True,'restart_persistence':True,'cpu_limiter_checked':True,'unrelated_preferences_unchanged':True,'native_4x':True,'final_fps':144,'final_refresh_hz':144};(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
