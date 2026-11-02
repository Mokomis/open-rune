"""Independent, big-endian replication fixtures. Does not import C++ serializers."""
import random
import struct
import subprocess
import sys
from pathlib import Path

rng = random.Random(92061)
header = lambda kind: struct.pack('!4sBI', b'ORR1', kind, 7)
def status(mode,winner,red,blue,phase,countdown,round_no,started,hb_count,red_ready,blue_ready):
    return struct.pack('!BBiiiiBBBIBBBBBB',mode,winner,red,blue,0,0,2,phase,countdown,round_no,started,hb_count,red_ready,blue_ready,0,0)
valid = []
invalid = []
for _ in range(200):
    ni, na = rng.randrange(9), rng.randrange(5)
    b = header(1) + struct.pack('!BBQQQB', ni, na, 1, 2, 3, 0)
    for i in range(ni):
        b += struct.pack('!QQIhhhHH', i+1, 12345, 1, rng.randrange(-1000,1001),
                         rng.randrange(-1000,1001), rng.randrange(-16384,16385), rng.randrange(65536), rng.randrange(8))
    for i in range(na):
        b += struct.pack('!QQQIB', i+1, i+1, 12345, 1, rng.randrange(1,17))
    valid.append(b)
    n = rng.randrange(65)
    valid.append(header(2)+struct.pack('!QB', 100, n)+b''.join(struct.pack('!QB', i+1, rng.randrange(1,11)) for i in range(n)))
    n = 512 if _ == 0 else rng.randrange(40)
    b = struct.pack('!HQQQHHB',6,1,0,100,n,0,0)
    for i in range(n):
        b += struct.pack('!IIBBhIiiihhhHHHHHhhhhIIIIIIB', i+1,1,1,3,100,1, i*8,-30,42, -4,0,4, 0,32768,65535, 1,2,16384,256, 0,0,0,0,0,0,0,0,255)
    valid.append(b)
    if n:
        movement = struct.pack('!IIIQHBHHHH', 1, 1, 1, 17, 2, 1, 3200, 2048, 2048, 256)
        valid.append(b[:30] + bytes([1]) + movement + b[31:])
        # Protocol 22 reuses the bounded 31-byte checkpoint for explicit falling.
        airborne = bytearray(b)
        airborne[40] = 1  # First entity flags: alive, not grounded.
        air_movement = struct.pack('!IIIQHBHHHH',1,1,1,17,2,2,3200,0,950,20000)
        valid.append(bytes(airborne[:30])+bytes([1])+air_movement+bytes(airborne[31:]))
        for ref,life,ticks,walking in [(n+1,1,2,1),(1,2,2,1),(1,1,256,1),(1,1,2,2)]:
            invalid.append(b[:30] + bytes([1]) + struct.pack('!IIIQHBHHHH',ref,1,life,17,ticks,walking,3200,2048,2048,256)+b[31:])
    count = (len(b)+639)//640
    for i in range(count):
        valid.append(header(3)+struct.pack('!QQQIBB',1,0,100,len(b),i,count)+b[i*640:(i+1)*640])
    move = rng.randrange(1, 3)
    contact = rng.randrange(1, 4)
    missed = contact == 1
    stagger = int(move == 2 and contact == 3 and bool(rng.randrange(2)))
    valid.append(header(4)+struct.pack('!QQQIIIIIIBBhB',_+1,100+_,_+1,1,1,
                   0 if missed else 2,0 if missed else 1,1,0 if missed else 1,
                   move,contact,0 if missed else rng.randrange(-20,101),stagger))
    if _ == 0:
        name=b'DM-Hildir'
        valid.append(header(5)+struct.pack('!HHIB',60,2,1,len(name))+name+bytes([0x31])*32+bytes([0x72])*32+bytes([1,2,1,0]))
        package, actor = b'RuneI', b'Ragnar'
        proxy=b'RunePlayerProxy'
        valid.append(header(6)+struct.pack('!QIIBBB',1,1,1,1,1,len(package))+package+bytes([len(actor)])+actor+
                     bytes([len(package)])+package+bytes([len(proxy)])+proxy+bytes([0]))
        valid.append(header(6)+struct.pack('!QIIBBBBBBB',2,1,1,2,1,0,0,0,0,0))
        valid.append(header(7)+struct.pack('!QB',17,9))
        valid += [header(8)+bytes([1]), header(8)+bytes([2])]
for n in (2,4,32):
    roster=b''.join(struct.pack('!IiHBBII',i+1,i-1,i,255,255,0,0) for i in range(n))
    valid.append(header(9)+struct.pack('!BIIIB',3,8,20,n,n)+roster+status(1,255,0,0,0,0,0,0,0,0,0))
name=b'DM-Thorstadt'
contract=struct.pack('!4sBIHHIB',b'ORR1',5,8,60,4,11,len(name))+name+bytes([0x31])*32+bytes([0x72])*32+bytes([1,2,1,0])
valid.append(header(10)+struct.pack('!I',8)+contract)
invalid += [header(9)+struct.pack('!BIIIB',2,8,20,1,2)+struct.pack('!IiHIiH',1,0,0,2,0,0),
            header(10)+struct.pack('!I',9)+contract]
valid.append(header(9)+struct.pack('!BIIIB',3,8,20,0,4)+
             b''.join(struct.pack('!IiHBBII',i+1,i,i,i%2,255,0,0) for i in range(4))+status(2,0,20,19,0,0,0,0,0,0,0))
valid.append(header(9)+struct.pack('!BIIIB',3,8,20,0,4)+
             b''.join(struct.pack('!IiHBBII',i+1,i,i,i%2,255,0,0) for i in range(4))+status(4,0,20,19,0,0,0,1,0,1,1))
for kind,package,actor,name in [(6,b'RuneI',b'RunePolyobj',b'RunePolyobj1'),(8,b'HeadBall',b'HBGamePump',b'HBGamePump0')]:
    valid.append(header(6)+struct.pack('!QIIBB',1,1,1,1,kind)+bytes([len(package)])+package+
                 bytes([len(actor)])+actor+bytes([0,0,len(name)])+name)
def entity(ref, kind=1, flags=3, owner=0, weapon=0, attach=255):
    return struct.pack('!IIBBhIiiihhhHHHHHhhhhIIIIIIB', ref,1,kind,flags,100,1,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,owner,int(bool(owner)),weapon,int(bool(weapon)),0,0,attach)
def native(off=3,flags=3,guard=255,meter=0,phase=0,age=0,cooldown=0,ready=0,rune_power=0,rune_capacity=0):
    return struct.pack('!IIBBBBHHBBB',off,int(bool(off)),flags,guard,meter,phase,age,cooldown,ready,rune_power,rune_capacity)
prefix=struct.pack('!HQQQHHB',6,1,0,100,3,0,0)+entity(1,flags=67,weapon=2)
suffix=entity(2,kind=2,owner=1,attach=12)+entity(3,kind=2,owner=1,attach=13)
valid.append(prefix+native()+suffix)
valid.append(prefix+native(flags=12,guard=0,meter=50,phase=3,age=520,cooldown=4000)+suffix)
valid.append(prefix+native(rune_power=75,rune_capacity=100)+suffix)
valid.append(prefix+native(ready=1)+suffix)
for args in [dict(off=2),dict(off=9),dict(flags=16),dict(meter=101),dict(phase=6),dict(age=1),dict(cooldown=4001),dict(ready=1,cooldown=1),dict(ready=1,phase=1)]:
    invalid.append(prefix+native(**args)+suffix)
# Canonical empty extension and pre-extension schema are rejected.
invalid.append(struct.pack('!HQQQHHB',6,1,0,100,1,0,0)+entity(1,flags=67)+native(off=0,flags=0,guard=0))
invalid.append(struct.pack('!HQQQHHB',5,1,0,100,1,0,0)+entity(1))
for b in valid:
    invalid += [b[:rng.randrange(len(b))], b+b'\0']
# Bounds/reserved fields/wrong epoch are independent negative fixtures.
invalid += [header(1)+struct.pack('!BBQQQB',9,0,0,0,0,0),
            header(8)+bytes([0]), header(8)+bytes([3]),
            header(2)+struct.pack('!QB',0,65),
            header(1)+struct.pack('!BBQQQB',0,0,0,0,1,0),
            header(1)+struct.pack('!BBQQQB',0,0,0,0,0,2),
            struct.pack('!4sBI',b'ORR1',1,8)+struct.pack('!BBQQQB',0,0,0,0,0,0),
            struct.pack('!HQQQHHB',6,1,0,1,513,0,0)]
# Two bounded skin batches carry authoritative skeletal appearance independently
# of interpolation snapshots. Texture references are package-local exports.
visual = header(6)+struct.pack('!QIIBBBB',1,1,1,3,1,0,8)+struct.pack('!BIHB',0,0,1024,2)+struct.pack('!BI',0,0)+struct.pack('!16i',*range(16))+bytes(range(50))
visual += (bytes([6])+b'RuneFX'+struct.pack('!I',1))*8
valid.append(visual)
invalid += [visual+b'X',visual[:28]+bytes([7])+visual[29:]]
bad_combat = header(4)+struct.pack('!QQQIIIIIIBBhB',1,100,1,1,1,2,1,1,1,1,2,100,0)
invalid += [bad_combat[:61]+bytes([2]), bad_combat[:57]+bytes([4])+bad_combat[58:],
            header(4)+struct.pack('!QQQIIIIIIBBhB',1,100,1,1,1,2,1,1,1,1,1,100,0)]
result = subprocess.run([sys.argv[1], 'decode'], input=''.join(b.hex()+'\n' for b in valid+invalid), text=True, capture_output=True, check=True)
expected = [b.hex() for b in valid]+['invalid']*len(invalid)
actual = result.stdout.splitlines()
assert actual == expected, next(((i, a, e) for i,(a,e) in enumerate(zip(actual,expected)) if a!=e), (len(actual),len(expected)))
if len(sys.argv)>2:
    folder=Path(sys.argv[2]); folder.mkdir(parents=True,exist_ok=True)
    for i,b in enumerate(valid): (folder/f'wire-{i}').write_bytes(b)
print(f'PASS independent replication wire: {len(valid)} valid, {len(invalid)} invalid')
