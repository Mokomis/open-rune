"""Launch distinct C++ processes over IPv4/IPv6 loopback, never a remote host."""
import subprocess
import sys

for family in ("4", "6"):
    flags = ["secure"] if len(sys.argv) > 2 else []
    server = subprocess.Popen([sys.argv[1], "server", family, *flags], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True)
    try:
        ready = server.stdout.readline().split()
        assert len(ready) == 2, (ready, server.stderr.read())
        client = subprocess.run([sys.argv[1], "client", family, *ready, *flags],
                                capture_output=True, text=True, timeout=12)
        stdout, stderr = server.communicate(timeout=12)
        assert client.returncode == 0, (client.stdout, client.stderr)
        assert server.returncode == 0, (stdout, stderr)
        assert "sent=100 received=100" in stdout and "sent=100 received=100" in client.stdout
        print("server:", stdout.strip())
        print("client:", client.stdout.strip())
    finally:
        if server.poll() is None:
            server.kill()
            server.communicate()
