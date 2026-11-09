const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),udp=require('node:dgram')
const {PassThrough}=require('node:stream')
const DHT=require('hyperdht'),p=require('./protocol.cjs'),{Discovery,atomic}=require('./peer.cjs')
const pause=ms=>new Promise(r=>setTimeout(r,ms))
const invite=(port,slot=2)=>`OPEN-RUNE-INVITE-5\n127.0.0.1 ${port}\nDM-Hildir\n${slot} 1\n${'a'.repeat(32)}\n${'b'.repeat(64)}\n3\n1\n2 1 0\n4095\n`
test('bounded control frames and strict invitation validation',async()=>{
  for(const value of ['x\n','{}\n{}\n','x'.repeat(4097)]){const s=new PassThrough();const result=p.frame(s,100);s.write(value);await assert.rejects(result);s.destroy()}
  const s=new PassThrough();const result=p.frame(s,10);await assert.rejects(result);s.destroy()
  assert.equal(p.invitation(invite(1234))[4],'2')
  assert.equal(p.invitation(invite(1234))[13],'4095')
  assert.throws(()=>p.invitation(invite(1234).replace('4095','4096')))
  assert.throws(()=>p.invitation(invite(1234).replace('4095','-1')))
  for(const bad of [invite(0),invite(1234,4),invite(1234).replace('127.0.0.1','999.0.0.1'),invite(1234)+'extra'])assert.throws(()=>p.invitation(bad))
})
test('real DHT discovery, concurrent leases, datagram boundaries and stale host removal',{timeout:40000},async()=>{
  const net=await require('hyperdht/testnet')(3)
  const dht={bootstrap:net.bootstrap,host:'127.0.0.1'},topic='open-rune/test/'+p.random(16).toString('hex')
  const directory=fs.mkdtempSync(path.join(os.tmpdir(),'rune-discovery-'))
  const hosts=[udp.createSocket('udp4'),udp.createSocket('udp4')]
  for(const h of hosts){await new Promise(r=>h.bind(0,'127.0.0.1',r));h.on('message',(data,from)=>h.send(data,from.port,from.address))}
  hosts.forEach((h,i)=>fs.writeFileSync(path.join(directory,`player${i+2}.rune-invite`),invite(h.address().port,i+2)))
  function heartbeat(){atomic(path.join(directory,'discovery-status.json'),JSON.stringify({protocol:p.GAME_PROTOCOL,updated:Date.now(),ready:true,map:'DM-Hildir',mode:'Deathmatch',players:1,capacity:3,occupied:[1]}))}
  heartbeat();const timer=setInterval(heartbeat,1000)
  const host=new Discovery({dht,topic,directory}),clients=Array.from({length:3},()=>new Discovery({dht,topic}))
  const sockets=[]
  try{
    await host.host();let rows=[];await clients[0].browse(r=>rows.push(r),8000)
    assert.equal(rows.length,1);assert.equal(rows[0].key,host.key);assert.equal(rows[0].players,1)
    const joins=await Promise.all(clients.slice(0,2).map(c=>c.join(host.key)))
    assert.notEqual(joins[0].slot,joins[1].slot)
    assert.notEqual(p.invitation(joins[0].invitation)[7],p.invitation(joins[1].invitation)[7])
    await assert.rejects(clients[2].join(host.key),/full/)
    for(let i=0;i<2;i++){
      const s=udp.createSocket('udp4');sockets.push(s);await new Promise(r=>s.bind(0,'127.0.0.1',r))
      const port=+p.invitation(joins[i].invitation)[2]
      const hello=Buffer.alloc(1200,i+1);hello[0]=22;hello[13]=1
      const got=new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('No datagram returned')),3000);s.once('message',d=>{clearTimeout(t);resolve(d)})})
      s.send(hello,port,'127.0.0.1');assert.deepEqual(await got,hello)
    }
    clients[0].leave();await pause(150)
    const rejoined=await clients[0].join(host.key);assert.equal(rejoined.slot,joins[0].slot)
    assert.equal(p.invitation(rejoined.invitation)[7],p.invitation(joins[0].invitation)[7])
    clients[0].leave();await pause(100)
    const previous=host.leases.get(joins[0].slot);previous.until=0;previous.lastGame=0;host.expire()
    const replacement=await clients[2].join(host.key)
    assert.equal(replacement.slot,joins[0].slot)
    assert.notEqual(p.invitation(replacement.invitation)[7],p.invitation(rejoined.invitation)[7])
    await host.close();rows=[];await clients[2].browse(r=>rows.push(r),4000);assert.equal(rows.length,0)
  }finally{clearInterval(timer);for(const s of sockets)s.close();for(const c of clients)await c.close();if(!host.stopped)await host.close();for(const h of hosts)h.close();await net.destroy();fs.rmSync(directory,{recursive:true,force:true})}
})

test('deadline and cancellation settle without a host',{timeout:5000},async()=>{
  const client=new Discovery({dht:{bootstrap:['127.0.0.1:1']}})
  try{
    const result=await client.browse(()=>assert.fail('Unexpected host'),100)
    assert.equal(result.timedOut,true)
    const join=client.join('1'.repeat(64));setTimeout(()=>client.leave(),50)
    await assert.rejects(join)
  }finally{await client.close()}
})

test('status rejects spoofed identities, incompatible protocols and unbounded metadata',()=>{
  const key='a'.repeat(64),nonce='b'.repeat(32)
  const good={protocol:p.PROTOCOL,gameProtocol:p.GAME_PROTOCOL,key,nonce,name:'Test game',mode:'Deathmatch',map:'DM-Hildir',players:1,capacity:4,available:3}
  assert.equal(p.status(good,nonce,key),good)
  for(const patch of [{key:'c'.repeat(64)},{nonce:'old'},{gameProtocol:p.GAME_PROTOCOL-1},{capacity:17},{players:5},{available:5},{name:'a'.repeat(49)},{map:'../Secrets'},{mode:'Other'}])
    assert.throws(()=>p.status({...good,...patch},nonce,key))
})

test('discovery topic and game version match the native protocol',()=>{
  const header=fs.readFileSync(path.join(__dirname,'../../Network/include/openrune/net/Protocol.h'),'utf8')
  const match=header.match(/ProtocolVersion\s*=\s*(\d+)/)
  assert.ok(match);assert.equal(p.GAME_PROTOCOL,+match[1])
  assert.equal(p.TOPIC,`open-rune/multiplayer/protocol-${p.GAME_PROTOCOL}/v1`)
})


test('Windows atomic publication survives a temporary reader lock',{skip:process.platform!=='win32'},async()=>{
  const {spawn}=require('node:child_process')
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'rune-atomic-')),file=path.join(dir,'state.json')
  fs.writeFileSync(file,'{"old":true}')
  const child=spawn('python',['-c','import sys,time; f=open(sys.argv[1],"rb"); print("LOCKED",flush=True); time.sleep(.12); f.close()',file])
  try{
    await new Promise((resolve,reject)=>{child.once('error',reject);child.stdout.once('data',resolve)})
    const start=Date.now();atomic(file,'{"new":true}')
    assert.deepEqual(JSON.parse(fs.readFileSync(file,'utf8')),{new:true})
    assert.ok(Date.now()-start>=30,'test reader actually held the destination open')
    await new Promise(resolve=>child.exitCode!==null?resolve():child.once('exit',resolve))
  }finally{if(child.exitCode===null)child.kill();fs.rmSync(dir,{recursive:true,force:true})}
})
