#!/usr/bin/env python3
"""Headless bot matches for measuring bot rule changes (A/B).

Each match runs a private local deathmatch host and N bots (default 4) in
separate processes, using normal authenticated input like any client. Half the
bots use the current rules and half `--rune-bot-brain=classic`, alternating by
slot; brains swap slots every match so slot, spawn and character luck cancel
out over many runs. Per-brain frags, deaths, hits, blocked attacks, misses,
damage and kills come from the host log (OPEN_RUNE_SCORE / OPEN_RUNE_CONTACT).
Game files and normal saves are not touched: every process gets its own config
and save directory under --out.

Example:
  bot_ab.py --binary build/SurrealEngine.app/Contents/MacOS/SurrealEngine \
      --game ~/rune-classic --out /tmp/matches --rounds 12 --seconds 150
"""
import argparse, json, os, re, subprocess, sys, time
from pathlib import Path

# Mid-sized deathmatch maps where four bots meet often.
MAPS = ['DM-Coliseum', 'DM-Hildir', 'DM-Thorstadt', 'DM-seaside', 'DM-Havlamahl', 'DM-Ruins']
BRAINS = ('current', 'classic')


def launch(binary, game, root, options):
    root.mkdir(parents=True)
    (root / 'saves').mkdir()
    (root / 'config' / 'SurrealEngine').mkdir(parents=True)
    log = (root / 'session.log').open('w')
    return subprocess.Popen([str(binary), '--direct', f'--save-directory={root / "saves"}', *options, str(game)],
                            env=dict(os.environ, SURREALENGINE_CONFIG_DIR=str(root / 'config')),
                            stdout=log, stderr=subprocess.STDOUT)


def run_match(a, index, game_map, brains):
    players = len(brains)
    session = a.out / f'match{index:03d}'
    invitations = session / 'invitations'
    host = launch(a.binary, a.game, session / 'host', [
        f'--rune-server={invitations}', '--rune-net-address=127.0.0.1', '--rune-net-mode=dm',
        f'--rune-net-maps={game_map}', f'--rune-net-players={players}', '--rune-net-teams=2', '--rune-net-arena-team-size=1',
        '--rune-net-frag-limit=0', f'--rune-net-time-limit={a.seconds}', '--rune-net-intermission=5',
        f'--rune-net-seconds={a.seconds + 60}'])
    deadline = time.monotonic() + 60
    while not (invitations / f'player{players}.rune-invite').exists():
        if host.poll() is not None or time.monotonic() > deadline:
            host.kill()
            return {'match': index, 'map': game_map, 'error': 'host did not start'}
        time.sleep(.1)
    bots = []
    for slot, brain in enumerate(brains, start=1):
        options = [f'--rune-join={invitations / f"player{slot}.rune-invite"}', '--rune-net-bot',
                   f'--rune-bot-difficulty={a.difficulty}', f'--rune-net-seconds={a.seconds + 25}']
        if brain == 'classic':
            options.append('--rune-bot-brain=classic')
        if a.debug:
            options.append('--rune-bot-debug')
        bots.append(launch(a.binary, a.game, session / f'bot{slot}', options))
    host_log = session / 'host' / 'session.log'
    ended = None
    deadline = time.monotonic() + a.seconds + 120
    while time.monotonic() < deadline:
        text = host_log.read_text(errors='replace')
        if 'OPEN_RUNE_MATCH_ENDED round=1' in text:
            ended = text
            break
        if host.poll() is not None:
            break
        time.sleep(1)
    # Bots print their AI counters when their own session time ends.
    for p in bots:
        try:
            p.wait(timeout=60)
        except subprocess.TimeoutExpired:
            pass
    for p in [*bots, host]:
        if p.poll() is None:
            p.terminate()
    for p in [*bots, host]:
        try:
            p.wait(timeout=20)
        except subprocess.TimeoutExpired:
            p.kill()
    text = ended or host_log.read_text(errors='replace')
    result = {'match': index, 'map': game_map, 'brains': brains, 'ended': ended is not None}
    # Final round-1 score per slot: pN=frags,deaths.
    scores = None
    for line in text.splitlines():
        if line.startswith('OPEN_RUNE_SCORE round=1 '):
            found = dict((int(s), (int(f), int(d))) for s, f, d in re.findall(r'p(\d+)=(-?\d+),(\d+)', line))
            if all(s in found for s in range(1, players + 1)):
                scores = found
    if scores:
        result['frags'] = [scores[s][0] for s in range(1, players + 1)]
        result['deaths'] = [scores[s][1] for s in range(1, players + 1)]
    # Per-slot combat from the host's contact log.
    combat = {s: dict(hits=0, blocked=0, misses=0, damage=0, kills=0) for s in range(1, players + 1)}
    health = {}
    for line in text.splitlines():
        life = re.match(r'OPEN_RUNE_PLAYER_LIFE player=(\d+) life=\d+ health=(-?\d+)', line)
        if life:
            health[int(life.group(1))] = int(life.group(2))
            continue
        m = re.match(r'OPEN_RUNE_CONTACT move=(\d) contact=(\d) health=(-?\d+) attacker=(\d+) target=(\d+)', line)
        if not m:
            continue
        move, contact, after, attacker, target = (int(v) for v in m.groups())
        if attacker not in combat:
            continue
        c = combat[attacker]
        if contact == 1:
            c['misses'] += 1
        elif contact == 2:
            c['blocked'] += 1
        else:
            c['hits'] += 1
            if target in health:
                c['damage'] += max(0, health[target] - after)
            health[target] = after
            if after <= 0:
                c['kills'] += 1
    result['combat'] = [combat[s] for s in range(1, players + 1)]
    for slot in range(1, players + 1):
        bot_log = (session / f'bot{slot}' / 'session.log').read_text(errors='replace')
        failure = re.search(r'DIRECT_LAUNCH_FAILED: (.*)', bot_log)
        if failure:
            result[f'bot{slot}_error'] = failure.group(1)
        stats = re.findall(r'OPEN_RUNE_BOT_AI (.*)', bot_log)
        if stats:
            result[f'bot{slot}_stats'] = stats[-1]
    return result


def summarize(results):
    fights = {b: dict(frags=0, deaths=0, hits=0, blocked=0, misses=0, damage=0, kills=0) for b in BRAINS}
    better = {b: 0 for b in BRAINS}
    better['tie'] = 0
    for r in results:
        brains = r.get('brains', [])
        for slot, brain in enumerate(brains):
            for k, v in r.get('combat', [{}] * len(brains))[slot].items():
                fights[brain][k] += v
            if 'frags' in r:
                fights[brain]['frags'] += r['frags'][slot]
                fights[brain]['deaths'] += r['deaths'][slot]
        if 'frags' in r:
            net = {b: sum(r['frags'][s] - r['deaths'][s] for s, x in enumerate(brains) if x == b) for b in BRAINS}
            better['current' if net['current'] > net['classic'] else 'classic' if net['classic'] > net['current'] else 'tie'] += 1
    usage = {b: {} for b in BRAINS}
    for r in results:
        for slot, brain in enumerate(r.get('brains', [])):
            for key, value in re.findall(r'(\w+)=(\d+)', r.get(f'bot{slot + 1}_stats', '')):
                if key != 'max_think_us':
                    usage[brain][key] = usage[brain].get(key, 0) + int(value)
    errors = [r for r in results if 'error' in r or any(k.endswith('_error') for k in r)]
    return {'matches': len(results), 'scored': sum('frags' in r for r in results),
            'better_net_score': better, 'totals': fights, 'usage': usage, 'errors': errors}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--game', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True, help='New directory for logs and results')
    p.add_argument('--rounds', type=int, default=12, help='Matches; brains swap slots each match')
    p.add_argument('--players', type=int, default=4, help='Bots per match (even)')
    p.add_argument('--seconds', type=int, default=150, help='Match time limit')
    p.add_argument('--difficulty', type=int, choices=range(4), default=2)
    p.add_argument('--parallel', type=int, default=2)
    p.add_argument('--maps', default=','.join(MAPS))
    p.add_argument('--debug', action='store_true', help='Pass --rune-bot-debug to bots (verbose AI traces)')
    a = p.parse_args()
    if a.players < 2 or a.players % 2:
        p.error('--players must be even')
    a.binary, a.game, a.out = a.binary.resolve(), a.game.resolve(), a.out.resolve()
    if a.out.exists():
        p.error('--out must be a new directory')
    a.out.mkdir(parents=True)
    maps = [m for m in a.maps.split(',') if m]
    jobs = []
    for i in range(a.rounds):
        first, second = BRAINS if i % 2 == 0 else BRAINS[::-1]
        jobs.append((i, maps[(i // 2) % len(maps)], [first if s % 2 == 0 else second for s in range(a.players)]))
    results = []
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(a.parallel) as pool, (a.out / 'results.jsonl').open('w') as out:
        for r in pool.map(lambda job: run_match(a, *job), jobs):
            results.append(r)
            out.write(json.dumps(r) + '\n')
            out.flush()
            print(json.dumps(r), flush=True)
    summary = summarize(results)
    (a.out / 'summary.json').write_text(json.dumps(summary, indent=2))
    print('SUMMARY ' + json.dumps(summary), flush=True)


if __name__ == '__main__':
    sys.exit(main())
