#!/usr/bin/env python3
"""Accelerated HeadBall carrier-to-goal check using owned game assets.

The fixture starts the round and supplies a head. The normal bot must navigate,
throw and score through authoritative physics and the original scoring zone.
This is not an end-to-end readiness/head-acquisition or rendered FPS test; use
`tactical_multiplayer.py --modes headball` for the normal multiplayer match.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','game','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--upper-start',action='store_true',help='Start on the goal roof to check the lower throwing approach')
    parser.add_argument('--spawn',type=int,help='Optional PlayerStart fixture index')
    args=parser.parse_args()
    args.binary=args.binary.resolve();args.game=args.game.resolve();args.output=args.output.resolve()
    args.output.mkdir(parents=True,exist_ok=True)
    command=[str(args.binary),'--direct','--rune-network-probe','--rune-net-native-combat',
             '--rune-bot-goal-probe','--probe-seed=73']
    if args.upper_start:command.append('--rune-bot-goal-upper')
    if args.spawn is not None:command.append('--rune-bot-goal-spawn='+str(args.spawn))
    with tempfile.TemporaryDirectory(prefix='open-rune-bot-goal-') as temporary:
        root=Path(temporary);(root/'saves').mkdir();(root/'config'/'SurrealEngine').mkdir(parents=True)
        command+=['--save-directory='+str(root/'saves'),str(args.game)]
        timed_out=False
        with (args.output/'run.log').open('w',encoding='utf-8') as log:
            try:
                result=subprocess.run(command,env=dict(os.environ,SURREALENGINE_CONFIG_DIR=str(root/'config')),
                    stdout=log,stderr=subprocess.STDOUT,timeout=180)
                returncode=result.returncode
            except subprocess.TimeoutExpired:
                timed_out=True;returncode=-1
    text=(args.output/'run.log').read_text(errors='replace')
    markers=[line for line in text.splitlines() if 'BOT_GOAL_' in line or 'DIRECT_LAUNCH_FAILED' in line]
    passed=returncode==0 and not timed_out and 'BOT_GOAL_OK' in text
    report=dict(passed=passed,returncode=returncode,timed_out=timed_out,markers=markers,
        binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),command=command,
        scope='fixture grants a head and starts the round; normal bot movement/throw/physics must score')
    (args.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)
    return 0 if passed else 1


if __name__=='__main__':raise SystemExit(main())
