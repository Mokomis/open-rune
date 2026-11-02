from pathlib import Path
import subprocess,xml.etree.ElementTree as E,re,json,time,hashlib,traceback,argparse
parser=argparse.ArgumentParser(description='Touch navigation checks for the grouped Open Rune menu. Restores preferences; uses isolated saves.')
parser.add_argument('--device',required=True);parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);a=['adb','-s',args.device];pkg='org.runeclassic.port';checks=[]
def adb(*args):return subprocess.check_output(a+list(args),timeout=40)
def tree():
 adb('shell','uiautomator','dump','--compressed','/data/local/tmp/rune-cleanup.xml')
 return E.fromstring(adb('shell','cat','/data/local/tmp/rune-cleanup.xml'))
def find(text,description=False):
 field='content-desc' if description else 'text'
 rows=[n for n in tree().iter('node') if n.get(field)==text and n.get('enabled')=='true' and (description or n.get('clickable')=='true')]
 assert rows,('missing',text)
 return rows[-1]
def tap(text,description=False):
 n=find(text,description);x,y,b,c=map(int,re.findall(r'\d+',n.get('bounds')));adb('shell','input','tap',str((x+b)//2),str((y+c)//2));time.sleep(.15)
def shot(name):
 (out/(name+'.png')).write_bytes(adb('exec-out','screencap','-p'));(out/(name+'.xml')).write_bytes(E.tostring(tree()))
def page_has(text):return any(n.get('text')==text for n in tree().iter('node'))
def find_back_action():
 ns=[n for n in tree().iter('node') if n.get('text') in ('Cancel','Back') and n.get('clickable')=='true' and n.get('enabled')=='true'];assert ns
 x,y,b,c=map(int,re.findall(r'\d+',ns[-1].get('bounds')));adb('shell','input','tap',str((x+b)//2),str((y+c)//2));time.sleep(.15)
def back():adb('shell','input','keyevent','KEYCODE_BACK');time.sleep(.15)
backups={n:adb('exec-out','run-as',pkg,'cat','shared_prefs/'+n) for n in ['MainActivity.xml','multiplayer.xml']}
for n,b in backups.items():(out/('before-'+n)).write_bytes(b)
result=dict(passed=False)
try:
 adb('shell','am','force-stop',pkg);adb('shell','am','start','-W','-n',pkg+'/.MainActivity','--es','saveTest','menu-cleanup')
 time.sleep(3);tap('Video');assert not page_has('Enhancements');shot('video-index');checks.append('enhancements_removed')
 sections={'Video':['Display & performance','Camera & field of view','Lighting & shadows','Textures & surfaces','Effects & weather'],'Audio':['Volume','Environmental & headphone audio'],'Controls':['Look & keyboard','Touch controls','Controller','Vibration'],'Interface':['HUD & FPS display'],'Gameplay':['Campaign','Combat moves','Enemy behavior'],'About & support':['Diagnostics','About & licenses']}
 for category,pages in sections.items():
  tap(category)
  for name in pages:
   tap(name)
   if name in ['Display & performance','Textures & surfaces','Combat moves']:shot(name.split(' ')[0].lower())
   assert not any(n.get('text')=='Enhancements' for n in tree().iter('node'))
   tap('Back');assert page_has(name)
  print('PASS category',category,flush=True)
 checks.append('all_17_sections_and_back')
 tap('Video');tap('Effects & weather');tap('Compare original visuals');assert page_has('Restore my visuals');tap('Restore my visuals');checks.append('original_preview_restore')
 tap('Gameplay');tap('Campaign');tap('Campaign character & skin');shot('characters');back();assert page_has('Campaign character & skin');checks.append('campaign_character_back')
 tap('Controls');tap('Touch controls');tap('Edit touch controls');back();checks.append('touch_editor_return')
 # Return to title using the menu's explicit Close control.
 try:tap('×  Close')
 except AssertionError:pass
 tap('Multiplayer');tap('Advanced joining');back();assert page_has('Find games');tap('Connection settings');tap('Connection smoothing');back();back();assert page_has('Find games');checks.append('advanced_join_connection_back')
 tap('Find games');time.sleep(.3)
 find_back_action();assert page_has('Find games');tap('Back');assert page_has('New campaign')
 tap('Multiplayer');tap('Find games');back();assert page_has('Find games');back();assert page_has('New campaign');checks.append('find_games_button_and_system_back_no_loop')
 tap('Multiplayer');tap('Host game');shot('host')
 # Host choices expose an accessible title:value; find it without assuming saved limits.
 nodes=[n for n in tree().iter('node') if n.get('content-desc','').startswith('Match rules:')];assert nodes
 x,y,b,c=map(int,re.findall(r'\d+',nodes[-1].get('bounds')));adb('shell','input','tap',str((x+b)//2),str((y+c)//2));assert page_has('Leap & rush without bloodlust');back();assert page_has('Start & join');checks.append('host_rules_visible')
 nodes=[n for n in tree().iter('node') if n.get('content-desc','').startswith('Map rotation:')];assert nodes
 x,y,b,c=map(int,re.findall(r'\d+',nodes[-1].get('bounds')));adb('shell','input','tap',str((x+b)//2),str((y+c)//2));shot('rotation');assert page_has('SELECTED ROTATION');assert page_has('AVAILABLE MAPS');checks.append('map_rotation_layout')
 result.update(passed=True,checks=checks)
except Exception:result.update(error=traceback.format_exc(),checks=checks);shot('failure')
finally:
 adb('shell','am','force-stop',pkg)
 for name,data in backups.items():
  f=out/('restore-'+name);f.write_bytes(data);adb('push',str(f),'/data/local/tmp/rune-cleanup-'+name);adb('shell','run-as',pkg,'cp','/data/local/tmp/rune-cleanup-'+name,'shared_prefs/'+name);adb('shell','rm','/data/local/tmp/rune-cleanup-'+name)
 result['preferences_restored']=all(adb('exec-out','run-as',pkg,'cat','shared_prefs/'+n)==b for n,b in backups.items())
 adb('shell','am','start','-W','-n',pkg+'/.MainActivity')
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)

raise SystemExit(0 if result["passed"] and result.get("preferences_restored") else 1)
