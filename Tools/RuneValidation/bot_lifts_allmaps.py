"""Inventory every multiplayer map and actually ride every discovered stop pair.

Requires original game data. Each server/client has separate saves and config.
The deterministic local file exchange checks replicated-world bot navigation,
not socket latency, rendered FPS, or arbitrary map objectives. Unplanned vertical
brushes remain in the report for review; they are never counted as passed rides.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("binary", "game", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--maps", nargs="*")
    return parser.parse_args()


def run_case(binary, game, folder, case):
    folder.mkdir(parents=True, exist_ok=False)
    exchange = folder / "exchange"
    exchange.mkdir()
    processes, files, commands, codes = [], [], [], []
    error = None
    started = time.monotonic()
    try:
        for role in ("server", "client"):
            target = folder / role
            target.mkdir()
            command = [str(binary), "--rune-network-probe", "--rune-net-native-combat",
                       "--rune-bot-traversal-probe", "--traversal-process=" + role,
                       "--traversal-exchange=" + str(exchange),
                       "--traversal-map=" + case["map"],
                       "--traversal-lift=" + case["actor"],
                       "--traversal-from=" + str(case["from"]),
                       "--traversal-to=" + str(case["to"]),
                       *case.get("options", []),
                       *(["--traversal-away"] if case["from"] else []),
                       "--save-directory=" + str(target / "saves"), str(game)]
            commands.append(command)
            log = (target / "game.log").open("w")
            files.append(log)
            processes.append(subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                             env=dict(os.environ, ALSOFT_DRIVERS="null",
                                      SURREALENGINE_CONFIG_DIR=str(target / "config"))))
        deadline = time.monotonic() + 90
        while any(process.poll() is None for process in processes):
            if any(process.poll() not in (None, 0) for process in processes):
                error = "peer failed; ending the paired diagnostic"
                break
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(commands, 90)
            time.sleep(.02)
    except subprocess.TimeoutExpired:
        error = "90-second case deadline"
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        for log in files:
            log.close()
    codes = [process.returncode for process in processes]
    texts = [(folder / role / "game.log").read_text(errors="replace")
             for role in ("server", "client")]
    server_marker, client_marker = (("BOT_ROAM_SERVER_OK", "BOT_ROAM_CLIENT_OK")
                                    if "--traversal-roam" in case.get("options", [])
                                    else ("BOT_LIFT_PROCESS_SERVER_OK", "BOT_LIFT_PROCESS_CLIENT_OK"))
    return dict(case, passed=codes == [0, 0]
                and server_marker in texts[0] and client_marker in texts[1],
                codes=codes, error=error, seconds=time.monotonic() - started,
                commands=commands,
                markers=[[line for line in text.splitlines()
                          if "PROCESS_" in line or "LIFT_SERVER_COMPLETE" in line
                          or "BOT_ROAM_" in line or "DIRECT_LAUNCH_FAILED" in line] for text in texts])


def main():
    args = arguments()
    binary, game, output = (getattr(args, key).resolve() for key in ("binary", "game", "output"))
    output.mkdir(parents=True, exist_ok=False)
    maps = sorted(path for path in (game / "Maps").glob("*.run")
                  if path.name.lower().startswith(("dm-", "ar-", "hb-"))
                  and (not args.maps or path.stem in args.maps))
    if not maps:
        raise SystemExit("No multiplayer map files selected")

    def scan(path):
        folder = output / "inventory" / path.stem
        folder.mkdir(parents=True)
        command = [str(binary), "--rune-network-probe", "--rune-net-native-combat",
                   "--rune-bot-traversal-probe", "--traversal-inventory", "--traversal-plans",
                   "--traversal-map=" + path.name,
                   "--save-directory=" + str(folder / "saves"), str(game)]
        with (folder / "game.log").open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=60,
                                    env=dict(os.environ, ALSOFT_DRIVERS="null",
                                             SURREALENGINE_CONFIG_DIR=str(folder / "config")))
        text = (folder / "game.log").read_text(errors="replace")
        plans, stops, movers, keys = set(), {}, {}, {}
        for line in text.splitlines():
            if line.startswith("BOT_LIFT_PLAN actor="):
                plans.add(re.search(r"actor=(\S+)", line)[1])
            elif line.startswith("TRAV_LANDING actor="):
                name = re.search(r"actor=(\S+)", line)[1]
                index = int(re.search(r"stop=(\d+)", line)[1])
                stops.setdefault(name, set()).add(index)
            elif line.startswith("TRAV_MOVER name="):
                movers[re.search(r"name=(\S+)", line)[1]] = line
            elif line.startswith("TRAV_KEY name="):
                name = re.search(r"name=(\S+)", line)[1]
                keys.setdefault(name, []).append(tuple(map(float, re.search(r"delta=(\S+)", line)[1].split(","))))
        vertical = [dict(actor=name, properties=movers[name], planned=name in plans)
                    for name, values in keys.items() if len(values) > 1
                    and max(abs(v[2] - values[0][2]) for v in values) >= 64
                    and max(abs(v[0] - values[0][0]) + abs(v[1] - values[0][1]) for v in values) < 1]
        return dict(map=path.name, map_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    passed=result.returncode == 0 and "BOT_LIFT_PLANS_OK" in text,
                    vertical_brushes=vertical, plans={name: sorted(stops.get(name, [])) for name in sorted(plans)},
                    command=command)

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        inventory = list(pool.map(scan, maps))
    report = dict(binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  scope="All selected multiplayer maps inventoried; actual separate-engine rides for every discovered stop pair; unplanned brushes require review",
                  inventory=inventory, cases=[])

    def save():
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")

    save()
    for row in inventory:
        for actor, stops in row["plans"].items():
            for start in stops:
                for end in stops:
                    if start == end:
                        continue
                    case = dict(map=row["map"], actor=actor, **{"from": start, "to": end})
                    name = row["map"][:-4] + "-" + actor + "-" + str(start) + "-" + str(end)
                    result = run_case(binary, game, output / "rides" / name, case)
                    report["cases"].append(result)
                    save()
                    print(name, "PASS" if result["passed"] else "FAIL", flush=True)
    report["all_discovered_rides_pass"] = all(row["passed"] for row in report["cases"])
    report["unplanned_vertical_brushes"] = sum(not brush["planned"] for row in inventory for brush in row["vertical_brushes"])
    save()


if __name__ == "__main__":
    main()
