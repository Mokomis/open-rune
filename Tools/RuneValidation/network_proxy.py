"""Private UDP impairment proxy; loopback by default, explicitly pinned LAN tests optional.
No credentials or payload logging.
"""
import heapq
import ipaddress
import random
import selectors
import socket
import threading
import time


class ImpairedLink:
    def __init__(self, server, *, uplink_ms=0, downlink_ms=0, jitter_ms=0,
                 loss=0, duplicate=0, seed=1, outage_after=0, outage_seconds=0,
                 bind_address='127.0.0.1', client_address='127.0.0.1'):
        # LAN playtests must explicitly name both local interface and tablet.
        # No wildcard listener, arbitrary destination or unrestricted relay.
        for address in (bind_address, client_address, server[0]):
            ip = ipaddress.IPv4Address(address)
            if not ip.is_private or ip.is_unspecified or ip.is_multicast:
                raise ValueError('Fixture requires explicit private IPv4 addresses')
        if server[0] != bind_address:
            raise ValueError('Server must use the same local interface as the proxy')
        self.client_address = client_address
        self.server = server
        self.delays = uplink_ms / 1000, downlink_ms / 1000
        self.jitter = jitter_ms / 1000
        self.loss, self.duplicate = loss / 100, duplicate / 100
        self.random = random.Random(seed)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((bind_address, 0))
        self.sock.setblocking(False)
        self.port = self.sock.getsockname()[1]
        self.client = None
        self.queue = []
        self.serial = 0
        self.stats = dict(received=0, forwarded=0, dropped=0, duplicated=0, peak_queue=0)
        self.outage_after, self.outage_seconds = outage_after, outage_seconds
        self.started = time.monotonic()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        with selectors.DefaultSelector() as selector:
            selector.register(self.sock, selectors.EVENT_READ)
            while not self.stop.is_set():
                now = time.monotonic()
                while self.queue and self.queue[0][0] <= now:
                    _, _, data, target = heapq.heappop(self.queue)
                    try:
                        self.sock.sendto(data, target)
                        self.stats['forwarded'] += 1
                    except BlockingIOError:
                        self.stats['dropped'] += 1
                for _key, _mask in selector.select(.002):
                    for _ in range(128):
                        try:
                            data, source = self.sock.recvfrom(65536)
                        except BlockingIOError:
                            break
                        down = source == self.server
                        if not down:
                            if source[0] != self.client_address or (self.client and self.client != source):
                                continue
                            self.client = source
                        target = self.client if down else self.server
                        if not target:
                            continue
                        self.stats['received'] += 1
                        age = now - self.started
                        outage = self.outage_after > 0 and self.outage_after <= age < self.outage_after + self.outage_seconds
                        if outage or self.random.random() < self.loss or len(self.queue) >= 4096:
                            self.stats['dropped'] += 1
                            continue
                        copies = 2 if self.random.random() < self.duplicate else 1
                        if copies == 2:
                            self.stats['duplicated'] += 1
                        for _ in range(copies):
                            self.serial += 1
                            delay = max(0, self.delays[down] + self.random.uniform(-self.jitter, self.jitter))
                            heapq.heappush(self.queue, (now + delay, self.serial, data, target))
                        self.stats['peak_queue'] = max(self.stats['peak_queue'], len(self.queue))

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        self.sock.close()
