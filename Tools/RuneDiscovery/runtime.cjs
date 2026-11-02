// One app-owned worklet. Commands/results live only in its private session directory.
const fs=require('#fs'),path=require('#path')
const {Discovery,atomic,json}=require('./peer.cjs')
const runtime=typeof Bare!=='undefined'?Bare:process
const config=json(runtime.argv[runtime.argv.length-1])
const stateFile=path.join(config.control,'state.json'),commandFile=path.join(config.control,'command.json')
const peer=new Discovery(config)
let closed=false,busy=false,lastCommand=0,generation=0,lastHeartbeat=0
const state={runtime:typeof Bare!=='undefined'?Bare.versions:process.versions,phase:'ready',message:'Ready',rows:[],sequence:0}
function publish(){state.updated=Date.now();state.sequence++;atomic(stateFile,JSON.stringify(state))}
function fail(e){if(closed)return;state.phase='error';state.message=String(e.message||e).slice(0,180);publish()}
runtime.on('uncaughtException',fail);runtime.on('unhandledRejection',fail)
async function stop(){if(closed)return;closed=true;clearInterval(pulse);await peer.close();state.phase='stopped';state.message='Stopped';publish()}
async function command(c){
  if(c.action==='stop'){await stop();return}
  if(c.action==='cancel'){generation++;peer.cancelSearch();if(state.phase==='joining')peer.leave();state.phase='ready';state.message='Search cancelled';publish();return}
  if(c.action==='leave'){generation++;peer.leave();state.phase='ready';state.message='Disconnected';publish();return}
  if(busy&&c.action==='join'&&state.phase==='searching'){
    generation++;peer.cancelSearch();while(busy)await new Promise(r=>setTimeout(r,5))
  }
  if(busy)return
  const mine=++generation;busy=true
  try{
    if(c.action==='browse'){
      state.phase='searching';state.message='Searching for games…';state.rows=[];publish()
      const result=await peer.browse(row=>{if(mine!==generation)return;state.rows.push(row);publish()})
      if(mine===generation){state.phase='results';state.message=result.timedOut?'Search timed out. Showing hosts that replied.':state.rows.length?'Choose a game to join.':'No games replied. Refresh to search again.';publish()}
    }else if(c.action==='join'){
      state.phase='joining';state.message='Connecting to the host…';state.key=c.key;publish()
      const result=await peer.join(c.key,()=>{if(mine===generation&&!closed){state.phase='lost';state.message='Peer connection lost. Rejoin from Multiplayer.';publish()}})
      if(mine!==generation){peer.leave();return}
      atomic(path.join(config.control,'joined.rune-invite'),result.invitation)
      state.phase='joined';state.message='Connected to '+result.host;state.key=result.key;state.joinId=c.id;state.slot=result.slot;publish()
    }
  }catch(e){if(mine===generation)fail(e)}finally{busy=false}
}
const pulse=setInterval(()=>{
  try{
    if(config.directory){peer.expire(peer.game(60000))}
    if(fs.existsSync(commandFile)){const c=json(commandFile);if(Number.isSafeInteger(c.id)&&c.id>lastCommand){lastCommand=c.id;command(c).catch(fail)}}
    if(Date.now()-lastHeartbeat>=1000){lastHeartbeat=Date.now();publish()}
  }catch(e){fail(e);if(config.directory)stop().catch(()=>{})}
},250)
publish()
if(config.directory){state.phase='publishing';state.message='Publishing this match…';publish();peer.host().then(key=>{if(!closed){state.key=key;state.phase='hosting';state.message='Listed in Find games';publish()}}).catch(fail)}
if(typeof Bare==='undefined'){process.on('SIGINT',()=>stop().finally(()=>process.exit()));process.on('SIGTERM',()=>stop().finally(()=>process.exit()))}
