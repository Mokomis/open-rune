#!/usr/bin/env python3
"""Exercise actual bot decisions, one shipped map per mode, without forced objectives.

Requires owned game assets. Uses bounded private hosts, isolated saves/config,
normal randomized characters and authenticated bot clients. Transport success
alone is not AI success: score and mode progression are checked separately.
"""
import argparse
import json
import re
from pathlib import Path
from types import SimpleNamespace
from private_multiplayer import run


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','game','output','scratch'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--modes',nargs='+',choices=('dm','tdm','arena','headball'),default=['dm','tdm','arena','headball'])
    parser.add_argument('--seconds',type=int,default=150)
    parser.add_argument('--seed',type=int,default=73)
    parser.add_argument('--impaired',action='store_true')
    parser.add_argument('--debug',action='store_true')
    options=parser.parse_args()
    for key in ('binary','game','output','scratch'):setattr(options,key,getattr(options,key).resolve())
    options.output.mkdir(parents=True,exist_ok=True);options.scratch.mkdir(parents=True,exist_ok=True)
    evidence=[]
    maps={'dm':'DM-Ruins','tdm':'DM-Hildir','arena':'AR-BladePit','headball':'HB-Agora'}
    for mode in options.modes:
        args=SimpleNamespace(**vars(options),players=4,abrupt=False,late_join=0,outage_seconds=0,
            server_option=[],characters=None,time_limit=options.seconds+30,stress=False,require_marker=[],
            bot_option=[f'--probe-seed={options.seed}',*(['--rune-bot-debug'] if options.debug else [])],tactical_bots=True,reconnect=False,expect_travel=False,all_maps=False)
        result=run(args,mode,[maps[mode]],mode)
        logs=options.output/mode
        server=(logs/'server.log').read_text()
        clients=[(logs/f'client{i}.log').read_text() for i in range(1,5)]
        metrics=[]
        for text in clients:
            rows=re.findall(r'OPEN_RUNE_BOT_AI (.+)',text)
            metrics.append(dict((key,int(value)) for key,value in re.findall(r'(\w+)=(\d+)',rows[-1])) if rows else {})
        checks={'transport':result['passed'],
            'all_bots_thought':all(row.get('decisions',0)>20 for row in metrics),
            'combat_requested':sum(row.get('attacks',0) for row in metrics)>=8,
            'authoritative_deaths':mode=='arena' or any(int(deaths)>0 for deaths in re.findall(r' p\d+=-?\d+,(\d+)',server)),
            'multiple_characters':len(set(re.findall(r'CHARACTER_VERIFIED class=(\w+) skin=(\d+)','\n'.join(clients))))>1}
        if mode in ('dm','tdm'):
            checks['each_bot_engaged']=all(row.get('attacks',0)+row.get('specials',0)>0 for row in metrics)
        if mode=='arena':
            checks['queue_reached']=bool(re.search(r'role\d+=[01],',server))
            checks['round_fought']='arena=1,' in server
            checks['round_resolved']='arena=3,' in server
        if mode=='headball':
            checks['teams_ready']='hb=0,' in server and bool(re.search(r'hb=0,\d+,1,1',server))
            checks['game_started']='OPEN_RUNE_HEADBALL_STARTED' in server
            checks['goal_scored']=any(int(red)>0 or int(blue)>0 for red,blue in re.findall(r'red=(-?\d+) blue=(-?\d+)',server))
            checks['head_thrown']=bool(re.search(r'OPEN_RUNE_ACTION player=\d+ kind=7 accepted=1',server))
        item={'mode':mode,'map':maps[mode],'passed':all(checks.values()),'checks':checks,'bot_requests':metrics,
            'binary_sha256':result['binary_sha256'],'seed':options.seed,'seconds':options.seconds,'impaired':options.impaired}
        evidence.append(item)
        (options.output/'ai-summary.json').write_text(json.dumps(evidence,indent=2)+'\n')
        print(json.dumps(item),flush=True)
    return 0 if all(item['passed'] for item in evidence) else 1

if __name__=='__main__':raise SystemExit(main())
