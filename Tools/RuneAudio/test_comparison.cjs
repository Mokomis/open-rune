// Headless browser regression: embedded previews decode and both buttons play.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {spawn,spawnSync}=require('node:child_process');
const {pathToFileURL}=require('node:url');
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function main(){
 const root=path.resolve(process.argv[2]);
 const preview=path.join(root,fs.existsSync(path.join(root,'preview-manifest.json'))?'preview-manifest.json':'manifest.json');
 const expected=JSON.parse(fs.readFileSync(preview,'utf8')).assets.length;
 const edge=process.argv[3]||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
 const profile=fs.mkdtempSync(path.join(os.tmpdir(),'open-rune-audio-browser-'));
 const child=spawn(edge,['--headless=new','--mute-audio','--no-first-run','--no-default-browser-check','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:'ignore'});
 let ws;
 try {
  let port;
  for(let n=0;n<200;n++){
   try{port=Number(fs.readFileSync(path.join(profile,'DevToolsActivePort'),'utf8').split('\n')[0]);if(port)break;}catch{}
   await sleep(100);
  }
  if(!port)throw Error('Browser did not start');
  const targets=await (await fetch('http://127.0.0.1:'+port+'/json/list')).json();
  ws=new WebSocket(targets.find(t=>t.type==='page').webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
  let sequence=0;const pending=new Map();
  ws.addEventListener('message',event=>{const m=JSON.parse(event.data);if(pending.has(m.id)){const {resolve,reject,timer}=pending.get(m.id);clearTimeout(timer);pending.delete(m.id);m.error?reject(Error(JSON.stringify(m.error))):resolve(m.result);}});
  function command(method,params={}){return new Promise((resolve,reject)=>{const id=++sequence;const timer=setTimeout(()=>{pending.delete(id);reject(Error('Timed out: '+method));},25000);pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method,params}));});}
  async function evaluate(expression){const r=await command('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true,userGesture:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;}
  await command('Page.navigate',{url:pathToFileURL(path.join(root,'comparison.html')).href});
  let ready=false;
  for(let n=0;n<100;n++){ready=await evaluate("document.readyState==='complete'&&document.querySelectorAll('article').length==="+expected);if(ready)break;await sleep(100);}
  if(!ready)throw Error('Comparison page did not load');
  const result=await evaluate(`(async()=>{
   const buttons=[...document.querySelectorAll('button')],ctx=new AudioContext();let decoded=0;
   for(const button of buttons){if(!button.dataset.src.startsWith('data:audio/wav;base64,'))throw Error('External media dependency');const bytes=await(await fetch(button.dataset.src)).arrayBuffer();const buffer=await ctx.decodeAudioData(bytes);if(!(buffer.duration>0)||buffer.numberOfChannels<1)throw Error('Invalid decoded media');decoded++;}
   await ctx.close();return {decoded,buttons:buttons.length};
  })()`);
  let played=0;
  for(let index=0;index<result.buttons;index++){
   await evaluate(`document.querySelectorAll('button')[${index}].click()`);
   await sleep(80);
   const state=await evaluate(`(()=>{const card=document.querySelectorAll('button')[${index}].closest('article'),audio=card.querySelector('audio');return {playing:!audio.paused&&!audio.error,at:audio.currentTime,status:card.querySelector('.playback-status').textContent};})()`);
   if(!state.playing||state.status)throw Error('Button '+index+' failed: '+JSON.stringify(state));played++;
  }
  console.log(JSON.stringify({...result,played,passed:true,browser:'Edge headless',physical_audio_output_tested:false},null,2));
 } finally {
  if(ws)ws.close();
  if(process.platform==='win32')spawnSync('taskkill',['/PID',String(child.pid),'/T','/F'],{stdio:'ignore'});else child.kill();
  await sleep(200);try{fs.rmSync(profile,{recursive:true,force:true});}catch{}
 }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
