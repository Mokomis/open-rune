"""Independent struct-based wire oracle; no import from the C++ implementation."""
import random
import struct
import subprocess
import sys

binary = sys.argv[1]


def packet(session, epoch, number, ranges, message=None):
    body = b"".join(struct.pack("!QQ", first, last) for first, last in ranges)
    if message:
        kind, lane, identity, timestamp, ttl, payload = message
        body += struct.pack("!BBHQQHH", kind, lane, 0, identity, timestamp, ttl, len(payload)) + payload
    return struct.pack("!4sHHQIQBBH", b"ORN1", 22, 0, session, epoch, number,
                       len(ranges), int(message is not None), len(body)) + body


golden = packet(0x0102030405060708, 0x11223344, 9, [(5, 7), (1, 2)],
                (1, 0, 3, 40, 100, bytes.fromhex("aabbcc")))
actual = subprocess.check_output([binary, "emit"], text=True).strip()
assert actual == golden.hex(), (actual, golden.hex())
rng = random.Random(392841)
fixtures = [golden]
for _ in range(1000):
    lane = rng.randrange(4)
    message = (lane + 1, lane, rng.randrange(1, 2**64-1), rng.randrange(2**64-1),
               rng.randrange(1, 1001) if lane in (0, 3) else 0, rng.randbytes(rng.randrange(769)))
    fixtures.append(packet(rng.randrange(1, 2**64), rng.randrange(1, 2**32),
                           rng.randrange(1, 2**64-1), [(90, 100), (1, 8)], message))
invalid = [golden[:i] for i in range(len(golden))]
invalid += [golden + b"\0", packet(0, 1, 1, []), packet(1, 0, 1, []),
            packet(1, 1, 0, []), packet(1, 1, 1, [(1, 2), (4, 5)]),
            packet(1, 1, 1, [(4, 5), (1, 3)]), packet(1, 1, 1, [(0, 1)]),
            packet(1, 1, 1, [], (2, 1, 1, 0, 1, b"")),
            packet(1, 1, 1, [], (1, 0, 1, 0, 10, bytes(769)))]
text = "".join(b.hex() + "\n" for b in fixtures + invalid)
result = subprocess.run([binary, "decode"], input=text, text=True, capture_output=True, check=True)
assert result.stdout.splitlines() == [b.hex() for b in fixtures] + ["invalid"] * len(invalid)
print(f"PASS independent wire oracle: {len(fixtures)} valid, {len(invalid)} invalid")
