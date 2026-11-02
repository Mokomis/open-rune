"""Launch the real windowed client loop twice against an isolated private server.

Scripted inputs enter the same engine window callbacks as live keyboard events.
This exercises rendering and networking together; it is not a human playtest.
Invitations and their random credentials live only in a private temporary folder.
"""
import argparse
import json
from network_proxy import ImpairedLink
import re
import subprocess
import tempfile
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--game", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--join-delay", type=int, default=0, help="Delay the second player to exercise the lobby")
parser.add_argument("--uplink-ms", type=int, default=0)
parser.add_argument("--downlink-ms", type=int, default=0)
parser.add_argument("--jitter-ms", type=int, default=0)
parser.add_argument("--loss", type=float, default=0)
parser.add_argument("--duplicate", type=float, default=0)
parser.add_argument("--outage-after", type=float, default=0)
parser.add_argument("--outage-seconds", type=float, default=0)
parser.add_argument("--expect-disconnect", action="store_true")
args = parser.parse_args()
if any(v < 0 or v > 500 for v in (args.uplink_ms,args.downlink_ms,args.jitter_ms)) or not 0 <= args.loss <= 100 or not 0 <= args.duplicate <= 100:
    parser.error("Invalid impairment bounds")
if not 0 <= args.join_delay <= 120: parser.error("join delay must be 0..120 seconds")
args.output.mkdir(parents=True, exist_ok=True)
binary, game = args.binary.resolve(), args.game.resolve()
processes = []
links = []
with tempfile.TemporaryDirectory(prefix="open-rune-interactive-") as temporary:
    root = Path(temporary)
    invitations = root / "invitations"
    logs = []
    def launch(name, options):
        saves = root / (name + "-saves")
        saves.mkdir()
        log = (args.output / (name + ".log")).open("w")
        logs.append(log)
        process = subprocess.Popen([str(binary), "--direct", f"--save-directory={saves}",
                                    *options, str(game)], stdout=log, stderr=subprocess.STDOUT)
        processes.append(process)
        return process
    try:
        server = launch("server", [f"--rune-server={invitations}", f"--rune-net-seconds={60+args.join_delay}", "--rune-net-test-arena"])
        deadline = time.monotonic() + 12
        while not (invitations / "player2.rune-invite").exists():
            if server.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("Server did not create invitations; see server.log")
            time.sleep(.05)
        for invitation in invitations.iterdir():
            assert invitation.stat().st_mode & 0o077 == 0, "Invitation must be private"
        clients = []
        for i in (1, 2):
            path = invitations / f'player{i}.rune-invite'
            lines = path.read_text().splitlines()
            address, port = lines[1].split()
            link = ImpairedLink((address, int(port)), uplink_ms=args.uplink_ms,
                downlink_ms=args.downlink_ms, jitter_ms=args.jitter_ms, loss=args.loss,
                duplicate=args.duplicate, seed=730+i, outage_after=args.outage_after,
                outage_seconds=args.outage_seconds)
            links.append(link)
            lines[1] = f'127.0.0.1 {link.port}'
            path.write_text('\n'.join(lines)+'\n')
        # Include first-use Vulkan pipeline compilation before the 17-second
        # action schedule; a cold shader cache must not truncate respawn.
        for i in (1, 2):
            if i==2 and args.join_delay: time.sleep(args.join_delay)
            clients.append(launch(f"client{i}", [f"--rune-join={invitations / f'player{i}.rune-invite'}",
                f"--rune-net-seconds={40+(args.join_delay if i==1 else 0)}", "--rune-net-test-input", "--render-msaa=2", "--maxfps=120",
                f"--capture-frame={(args.output / f'client{i}.ppm').resolve()}"]))
        for client in clients:
            assert client.wait(timeout=60) == 0, "Client failed; inspect its log"
        server.wait(timeout=60)
        server_text = (args.output / "server.log").read_text()
        assert "OPEN_RUNE_MATCH_STARTED" in server_text
        if args.expect_disconnect:
            assert "OPEN_RUNE_PLAYER_DISCONNECTED" in server_text or "OPEN_RUNE_PLAYER_LEFT" in server_text
            for i in (1, 2):
                text = (args.output / f"client{i}.log").read_text()
                assert "OPEN_RUNE_CLIENT_JOINED" in text and "OPEN_RUNE_CLIENT_STOP" in text
            print("PASS authenticated outage ends match cleanly")
            raise SystemExit(0)
        assert "OPEN_RUNE_CONTACT move=1 contact=3" in server_text
        assert "kind=8 accepted=1" in server_text, "Jump did not reach server"
        assert "kind=7 accepted=1" in server_text, "Throw did not reach server"
        assert "kind=9 accepted=1" in server_text, "Respawn did not reach server"
        for i in (1, 2):
            text = (args.output / f"client{i}.log").read_text()
            assert f"OPEN_RUNE_CLIENT_JOINED owned={i}" in text
            match = re.search(r"OPEN_RUNE_CLIENT_STOP frames=(\d+) worlds=(\d+) contacts=(\d+) movement=([\d.]+)", text)
            assert match and int(match[1]) > 60 and int(match[2]) > 30 and int(match[3]) >= 1 and float(match[4]) > 20
            prediction = re.search(r"OPEN_RUNE_PREDICTION enabled=1 frames=(\d+) lead_frames=(\d+) max_lead=([\d.]+)", text)
            assert prediction and int(prediction[1]) > 30 and int(prediction[2]) > 0, "No predicted movement"
            assert (args.output / f"client{i}.ppm").stat().st_size > 10000
            print(f"Player {i}: {match[1]} rendered frames, {match[2]} worlds, {match[3]} combat results, moved {match[4]} units")
        print("PASS interactive server + two rendered clients, native window resolution, 4x AA")
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
            process.wait()
        for log in logs:
            log.close()
        for link in links:
            link.close()
        (args.output / 'impairments.json').write_text(json.dumps({'settings': {
            k: v for k, v in vars(args).items() if not isinstance(v, Path)},
            'links': [link.stats for link in links]}, indent=2)+'\n')
