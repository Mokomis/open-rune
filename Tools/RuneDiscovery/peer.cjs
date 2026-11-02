const DHT = require('hyperdht')
const fs = require('#fs')
const path = require('#path')
const udp = require('#dgram')
const p = require('./protocol.cjs')
function atomic(file, data) { fs.writeFileSync(file+'.tmp',data,{mode:0o600}); fs.renameSync(file+'.tmp',file) }
function json(file) { return JSON.parse(fs.readFileSync(file,'utf8')) }
const delay = ms => new Promise(r=>setTimeout(r,ms))
function safe(socket) { socket.on('error',()=>{}) ; return socket }
async function bind(address='127.0.0.1') {
  const socket=udp.createSocket('udp4'); safe(socket)
  await new Promise((resolve,reject)=>{socket.once('error',reject);socket.bind(0,address,()=>{socket.removeListener('error',reject);resolve()})})
  return socket
}
function closeUdp(socket) { if(socket)try{socket.close()}catch{} }
// Unordered authenticated messages, never the reliable byte stream. DTLS remains intact.
function bridge(socket, datagram, target) {
  let window=Date.now(), packets=0, bytes=0
  const allowed=data=>{const now=Date.now();if(now-window>=1000){window=now;packets=bytes=0}return data.length>0&&data.length<=1200&&++packets<=2048&&(bytes+=data.length)<=2*1024*1024}
  const receive=data=>{if(allowed(data)&&target.port)datagram.send(data,target.port,target.address,()=>{})}
  const transmit=(data,from)=>{if(from.address===target.address&&from.port===target.port&&data.length<=1200&&socket.connected&&!socket.destroyed)socket.trySend(data)}
  socket.on('message',receive);datagram.on('message',transmit)
  return ()=>{socket.removeListener('message',receive);datagram.removeListener('message',transmit)}
}
class Discovery {
  constructor(options={}) {
    this.options=options;this.topic=options.topic||p.TOPIC;this.stopped=false;this.sockets=new Set();this.leases=new Map();this.search=null;this.client=null;this.pendingJoin=null
    this.dht=new DHT({ephemeral:true,port:[0,0],...(options.dht||{})});this.dht.on('error',()=>{})
    this.keyPair=DHT.keyPair();this.key=this.keyPair.publicKey.toString('hex')
  }
  track(s) {safe(s);this.sockets.add(s);s.once('close',()=>this.sockets.delete(s));return s}
  game(maxAge=5000) {
    const state=json(path.join(this.options.directory,'discovery-status.json'))
    if(!state.ready || Date.now()-state.updated>maxAge || state.protocol!==p.GAME_PROTOCOL) throw new Error('Host is not ready.')
    return state
  }
  expire(state=this.game(60000)) {
    const now=Date.now()
    for(const [slot,lease] of this.leases){
      if(state.occupied.includes(slot))lease.lastGame=now
      if((!lease.socket&&now>lease.until)||(now-lease.lastGame>45000)){
        if(lease.socket)lease.socket.destroy()
        closeUdp(lease.udp);this.leases.delete(slot)
      }
    }
  }
  available(state) {
    this.expire(state);let n=0
    for(let slot=this.options.firstSlot===1?1:2;slot<=state.capacity;slot++)if(!this.leases.has(slot)&&!state.occupied.includes(slot))n++
    return n
  }
  async host() {
    this.game()
    this.server=this.dht.createServer({firewall:()=>this.sockets.size>=32},socket=>this.accept(this.track(socket)))
    await this.server.listen(this.keyPair)
    const announce=()=>this.dht.announce(p.topicKey(this.topic),this.keyPair,this.server.nodes).finished()
    // Listen immediately; publication can take longer while a route is bootstrapping.
    this.refresh=setInterval(()=>{this.expire();announce().catch(()=>{})},60000)
    await announce()
    return this.key
  }
  async accept(socket) {
    let joined=false
    socket.setKeepAlive(3000);socket.setTimeout(15000)
    try {
      if(typeof socket.rawStream.trySend!=="function")throw new Error("This route cannot carry game packets.");
      const request=await p.frame(socket)
      if(request.protocol!==p.PROTOCOL || !/^[0-9a-f]{32}$/.test(request.nonce) || !['status','join'].includes(request.action))throw new Error('Invalid request.')
      const state=this.game(), available=this.available(state)
      const reply={protocol:p.PROTOCOL,gameProtocol:p.GAME_PROTOCOL,key:this.key,nonce:request.nonce,name:this.options.name||'Open Rune match',mode:state.mode,map:state.map,players:state.players,capacity:state.capacity,available}
      if(request.action==='status') {socket.end(JSON.stringify(reply)+'\n');return}
      const remote=socket.remotePublicKey.toString('hex')
      let entry=[...this.leases].find(([slot,l])=>l.owner===remote)
      if(entry&&entry[1].socket&&!entry[1].socket.destroyed)throw new Error('Already connected to this match.')
      if(!entry) {
        const first=this.options.firstSlot===1?1:2
        const slot=Array.from({length:state.capacity-first+1},(_,i)=>i+first).find(i=>!this.leases.has(i)&&!state.occupied.includes(i))
        if(!slot)throw new Error('This match is full. Try again shortly.')
        // Reserve before any await: concurrent joins must never receive the same slot.
        const lease={owner:remote,socket,until:Infinity,lastGame:Date.now(),udp:null};this.leases.set(slot,lease);entry=[slot,lease]
        try {
          const file=path.join(this.options.directory,'player'+slot+'.rune-invite')
          const fields=p.invitation(fs.readFileSync(file,'utf8'))
          lease.target={address:fields[1],port:+fields[2]};lease.udp=await bind(fields[1])
          fields[6]=p.random(16).toString('hex');fields[7]=p.random(32).toString('hex')
          atomic(file,p.encode(fields));lease.invitation=p.encode(fields)
        }catch(e){this.leases.delete(slot);closeUdp(lease.udp);throw e}
      }
      const [slot,lease]=entry
      if(socket.destroyed)throw new Error('Connection closed.')
      lease.socket=socket;lease.until=Infinity
      const detach=bridge(socket,lease.udp,lease.target)
      socket.once('close',()=>{detach();if(lease.socket===socket){lease.socket=null;lease.until=Date.now()+45000}})
      joined=true
      p.send(socket,{...reply,invitation:lease.invitation,slot})
      // After admission the ordered channel only carries keepalives.
      socket.on('data',()=>socket.destroy())
    } catch(e) {
      if(!socket.destroyed)socket.end(JSON.stringify({error:e.message})+'\n')
    } finally {
      if(!joined) {
        for(const lease of this.leases.values())if(lease.socket===socket){lease.socket=null;lease.until=Date.now()+45000}
      }
    }
  }
  cancelSearch() {if(this.search)this.search();this.search=null}
  async browse(onRow, deadline=20000) {
    this.cancelSearch()
    const query=this.dht.lookup(p.topicKey(this.topic)), seen=new Set(), queue=[], running=new Set(), sockets=new Set()
    let done=false,ended=false,wake, timedOut=false
    const finished=new Promise(r=>wake=r)
    const maybe=()=>{if(ended&&!running.size&&!queue.length)wake()}
    const cancel=()=>{done=true;ended=true;queue.length=0;query.destroy();for(const s of sockets)s.destroy();maybe()}
    this.search=cancel
    const launch=()=>{
      while(!done&&running.size<4&&queue.length) {
        const peer=queue.shift(),key=peer.publicKey.toString('hex')
        const task=(async()=>{
          const s=this.track(this.dht.connect(peer.publicKey,{nodes:peer.nodes}));sockets.add(s)
          const nonce=p.random(16).toString('hex'),start=Date.now()
          try{const response=p.frame(s);p.send(s,{protocol:p.PROTOCOL,nonce,action:'status'});const row=p.status(await response,nonce,key);if(!done)onRow({...row,responseMs:Date.now()-start})}catch{}finally{sockets.delete(s);s.destroy()}
        })()
        running.add(task);task.finally(()=>{running.delete(task);launch();maybe()})
      }
    }
    query.on('data',reply=>{for(const peer of reply.peers){const key=peer.publicKey.toString('hex');if(done||key===this.key||seen.has(key)||seen.size>=64)continue;seen.add(key);queue.push(peer)}launch()})
    query.on('error',()=>{});query.on('close',()=>{ended=true;maybe()});query.resume()
    const timer=setTimeout(()=>{timedOut=true;cancel()},deadline)
    await finished;clearTimeout(timer);if(this.search===cancel)this.search=null
    return {timedOut,candidates:seen.size}
  }
  leave() {if(this.pendingJoin){this.pendingJoin.destroy();this.pendingJoin=null}if(this.client){this.client.detach();this.client.socket.destroy();closeUdp(this.client.udp);this.client=null}}
  async join(key, onClose=()=>{}) {
    if(!p.KEY.test(key))throw new Error('Invalid host identity.')
    this.cancelSearch();this.leave()
    const socket=this.track(this.dht.connect(p.b.from(key,'hex')));this.pendingJoin=socket;socket.setKeepAlive(3000);socket.setTimeout(15000)
    let datagram
    try {
      const nonce=p.random(16).toString('hex'),response=p.frame(socket,15000)
      p.send(socket,{protocol:p.PROTOCOL,nonce,action:'join'})
      const reply=await response
      if(reply.error)throw new Error(typeof reply.error==='string'?reply.error.slice(0,100):'Host refused the join.')
      p.status(reply,nonce,key)
      if(typeof socket.rawStream.trySend!=="function")throw new Error("This route cannot carry game packets.");
      const fields=p.invitation(reply.invitation)
      if(+fields[4]!==reply.slot || +fields[4]<1 || +fields[8]!==reply.capacity)throw new Error('Invalid assigned slot.')
      datagram=await bind();const target={address:'127.0.0.1',port:0}
      const learn=(data,from)=>{if(!target.port&&from.address==='127.0.0.1'&&data.length>=25&&data[0]===22&&data[13]===1)target.port=from.port}
      datagram.on('message',learn)
      const detach=bridge(socket,datagram,target)
      socket.on('data',()=>socket.destroy())
      if(socket.destroyed)throw new Error("Join cancelled.");
      this.pendingJoin=null;const client={socket,udp:datagram,detach};this.client=client
      socket.once('close',()=>{detach();closeUdp(datagram);if(this.client===client){this.client=null;onClose()}})
      fields[1]='127.0.0.1';fields[2]=String(datagram.address().port)
      return {invitation:p.encode(fields),key,slot:reply.slot,host:reply.name}
    }catch(e){if(this.pendingJoin===socket)this.pendingJoin=null;socket.destroy();closeUdp(datagram);throw e}
  }
  async close() {
    this.stopped=true;this.cancelSearch();this.leave();clearInterval(this.refresh)
    for(const s of this.sockets)s.destroy()
    for(const l of this.leases.values())closeUdp(l.udp)
    this.leases.clear()
    if(this.server)await Promise.race([this.dht.unannounce(p.topicKey(this.topic),this.keyPair).catch(()=>{}),delay(1500)])
    await this.dht.destroy({force:true})
  }
}
module.exports={Discovery,atomic,json}
