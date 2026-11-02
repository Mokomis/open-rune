"""Isolated Mac stair run through authenticated clients. Never uses a device or live host."""
import argparse, csv, json, math, os, subprocess, time
from pathlib import Path
from network_proxy import ImpairedLink
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,required=True);p.add_argument('--game',type=Path,required=True)
p.add_argument('--output',type=Path,required=True);p.add_argument('--latency',type=int,default=0)
p.add_argument('--smooth',type=int,choices=(0,1),default=1);p.add_argument('--captures',action='store_true')
p.add_argument('--check',action='store_true')
p.add_argument('--fps',type=int,default=120)
p.add_argument('--smoothing',type=int,choices=(0,1),default=1)
p.add_argument('--render-both',action='store_true',help='Render the second peer too; default keeps a single foreground window')
p.add_argument('--route',default='220.203,-773.538,69,0')
a=p.parse_args()
if not 0<=a.latency<=100 or not 20<=a.fps<=240:p.error("latency must be 0..100 ms and fps 20..240")
a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False)
procs=[];logs=[];links=[]
def launch(name,args):
    root=a.output/name;root.mkdir();(root/'saves').mkdir();(root/'config'/'SurrealEngine').mkdir(parents=True)
    log=(root/'run.log').open('w');logs.append(log)
    proc=subprocess.Popen([str(a.binary.resolve()),'--direct',f'--save-directory={root/"saves"}',*args,str(a.game.resolve())],stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(root/'config')))
    procs.append(proc);return proc
try:
    invitations=a.output/'invitations'
    server=launch('server',[f'--rune-server={invitations}','--rune-net-address=127.0.0.1','--rune-net-seconds=45','--probe-seed=1',f'--rune-net-stair-route={a.route}'])
    deadline=time.monotonic()+20
    while not (invitations/'player2.rune-invite').exists():
        if server.poll() is not None or time.monotonic()>deadline:raise RuntimeError('server failed')
        time.sleep(.1)
    clients=[]
    for i in (1,2):
        invite=invitations/f'player{i}.rune-invite'
        if a.latency:
            lines=invite.read_text().splitlines();host,port=lines[1].split()
            link=ImpairedLink((host,int(port)),uplink_ms=a.latency,downlink_ms=a.latency,jitter_ms=8,loss=1,duplicate=1,seed=782+i)
            links.append(link);lines[1]=f'127.0.0.1 {link.port}';invite.write_text('\n'.join(lines)+'\n')
        flags=[f'--rune-join={invite}','--rune-net-seconds=22','--render-msaa=2',f'--maxfps={a.fps}','--profile-frames=900',f'--rune-net-stair-smoothing={a.smooth}',f'--rune-net-smoothing={a.smoothing}']
        if i==2 and not a.render_both:flags+=['--rune-net-bot','--rune-net-bot-idle']
        if i==1:
            flags+=['--rune-net-stair-input',f'--rune-net-stair-trace={a.output/"movement.csv"}']
            if a.captures:
                (a.output/'frames').mkdir();flags+=[f'--rune-net-stair-captures={a.output/"frames"}']
        clients.append(launch(f'client{i}',flags))
    for proc in clients:
        if proc.wait(timeout=45):raise RuntimeError('client failed')
finally:
    for proc in reversed(procs):
        if proc.poll() is None:proc.terminate()
    for proc in procs:
        try:proc.wait(timeout=5)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
    for link in links:link.close()
    for log in logs:log.close()

rows=list(csv.DictReader((a.output/'movement.csv').open()))
yaw=float(a.route.split(',')[-1])*math.tau/65536
forward=lambda r: float(r['x'])*math.cos(yaw)+float(r['y'])*math.sin(yaw)
metrics={}
for name,begin,end,sign in [('ascent',1800,4000,1),('descent',6000,8200,-1)]:
    section=[r for r in rows if begin<float(r['ms'])<end]
    assert len(section)>1, f'{name}: incomplete run; inspect client logs'
    pairs=list(zip(section,section[1:]))
    progress=[sign*(forward(b)-forward(c)) for c,b in pairs]
    relative=[abs((float(b['visual_z'])-float(b['camera_z']))-(float(c['visual_z'])-float(c['camera_z']))) for c,b in pairs
              if b['camera_blocked']=='0' and c['camera_blocked']=='0']
    metrics[name]={'frames':len(section),'travel':sign*(forward(section[-1])-forward(section[0])),
        'worst_reverse':max(0,-min(progress)),'reversals_over_3':sum(v< -3 for v in progress),
        'unobstructed_pairs':len(relative),'max_body_camera_vertical_jolt':max(relative,default=0),
        'height_range':max(float(r['z']) for r in section)-min(float(r['z']) for r in section)}
    (a.output/'result.json').write_text(json.dumps(metrics,indent=2)+'\n')
    if a.check:
        assert len(section)>min(60,a.fps) and metrics[name]['travel']>200 and metrics[name]['height_range']>100,metrics
        assert len(relative)>min(20,a.fps//2), 'insufficient unobstructed camera coverage'
        assert metrics[name]['worst_reverse']<8 and metrics[name]['max_body_camera_vertical_jolt']<2,metrics
(a.output/'result.json').write_text(json.dumps(metrics,indent=2)+'\n')
print(json.dumps(metrics,indent=2))
