"""Two isolated Open Rune processes, authenticated UDP on IPv4 loopback only."""
import argparse
import select
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--game", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--natural", action="store_true", help="Wait for Rune animation impacts instead of triggering frames")
parser.add_argument("--two-clients", action="store_true", help="Give both duel pawns independent remote owners")
parser.add_argument("--latency-ms", type=int, default=0, help="One-way datagram delay per process")
parser.add_argument("--loss-percent", type=int, default=0)
args = parser.parse_args()
if args.two_clients and not args.natural:
    parser.error("--two-clients requires --natural")
args.output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="open-rune-duel-saves-") as saves:
    def command(role, port=None):
        path = Path(saves, role)
        path.mkdir()
        result = [str(args.binary), "--direct", f"--rune-network-process-probe={role}",
                  f"--save-directory={path}",f"--rune-network-process-latency={args.latency_ms}",
                  f"--rune-network-process-loss={args.loss_percent}"]
        if port is not None:
            result.append(f"--rune-network-process-port={port}")
        return result + [str(args.game)]

    suffix="-natural" if args.natural else ""
    server_role = "server-dual-natural" if args.two_clients else "server"+suffix
    server = subprocess.Popen(command(server_role), stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True, bufsize=1)
    try:
        ready, _, _ = select.select([server.stdout], [], [], 8)
        if not ready:
            raise RuntimeError("server did not publish its loopback port")
        first = server.stdout.readline()
        if not first.startswith("PORT "):
            raise RuntimeError(f"server startup: {first!r}")
        port = int(first.split()[1])
        second_line = ""
        second_port = None
        if args.two_clients:
            second_line = server.stdout.readline()
            if not second_line.startswith("PORT2 "):
                raise RuntimeError(f"second server port: {second_line!r}")
            second_port = int(second_line.split()[1])
        second_client = None
        if args.two_clients:
            second_client = subprocess.Popen(command("client-two-natural", second_port),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        client = subprocess.run(command("client"+suffix, port), capture_output=True,
                                text=True, timeout=22)
        second_output = second_errors = ""
        if second_client:
            second_output, second_errors = second_client.communicate(timeout=22)
        rest, server_errors = server.communicate(timeout=22)
        server_output = first + second_line + rest
        (args.output / "server.log").write_text(server_output + server_errors)
        (args.output / "client.log").write_text(client.stdout + client.stderr)
        if second_client:
            (args.output / "client-two.log").write_text(second_output + second_errors)
        assert server.returncode == 0, (server_output, server_errors)
        assert client.returncode == 0, (client.stdout, client.stderr)
        if second_client:
            assert second_client.returncode == 0, (second_output, second_errors)
        assert "PASS ENGINE_DUEL_SERVER kick=1 bash=1" in server_output
        assert "PASS ENGINE_DUEL_CLIENT kick=1 bash=1" in client.stdout
        if second_client:
            assert "PASS ENGINE_DUEL_CLIENT kick=1 bash=1" in second_output
        print(server_output.strip())
        print(client.stdout.strip())
        if second_client:
            print(second_output.strip())
    finally:
        if server.poll() is None:
            server.kill()
            server.communicate()
