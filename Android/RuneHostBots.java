package org.runeclassic.port;

import android.content.*;
import android.os.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import org.json.*;

/** Host-owned tactical clients; bindings keep them alive with the foreground host. */
final class RuneHostBots implements AutoCloseable {
    private final Context context;
    private final File session;
    private final int capacity;
    private final boolean publicMatch;
    private final ArrayList<Bot> bots=new ArrayList<>();
    private final long[] retired=new long[15];
    private int queued;
    private boolean closed;
    String message="";
    private final class Bot implements ServiceConnection {
        final int component;final File root;
        RuneDiscovery discovery;boolean bound,joined,removed;int slot;
        final long began=SystemClock.elapsedRealtime();
        Bot(int component) {this.component=component;root=new File(session,"bots/"+UUID.randomUUID());root.mkdirs();}
        public void onServiceConnected(ComponentName name,IBinder binder){}
        public void onServiceDisconnected(ComponentName name){fail(this,"A bot disconnected. Add another when a slot becomes available.");}
        public void onBindingDied(ComponentName name){fail(this,"A bot stopped. Add another when a slot becomes available.");}
        public void onNullBinding(ComponentName name){fail(this,"The bot could not start.");}
        void bind(File invitation)throws Exception {
            if(removed||closed)return;
            Class<?> type=Class.forName("org.runeclassic.port.RuneBotService$Bot"+(component+1));
            Intent intent=new Intent(context,type).putExtra("directory",root.getAbsolutePath()).putExtra("invitation",invitation.getAbsolutePath());
            bound=context.bindService(intent,this,Context.BIND_AUTO_CREATE|Context.BIND_IMPORTANT);
            if(!bound)throw new IOException("Android could not start a bot process.");
        }
        void close(){
            if(removed)return;removed=true;
            if(discovery!=null)discovery.close();
            if(bound){try{context.unbindService(this);}catch(IllegalArgumentException ignored){}bound=false;}
            retired[component]=SystemClock.elapsedRealtime();
        }
    }
    RuneHostBots(Context context,File session,int capacity,int starting,boolean publicMatch){this.context=context;this.session=session;this.capacity=capacity;this.publicMatch=publicMatch;queued=starting;}
    int count(){return queued+bots.size();}
    void add(){
        if(closed)return;
        if(count()>=capacity-1){message="The player limit includes you and the bots.";return;}
        queued++;message="Adding a bot…";
    }
    void remove(){
        if(queued>0)queued--;else if(!bots.isEmpty()){Bot bot=bots.remove(bots.size()-1);bot.close();}else{message="No bots to remove.";return;}
        message="Bot removed. Its player slot may take a moment to become available.";
    }
    private void fail(Bot bot,String detail){if(bots.remove(bot)){bot.close();message=detail;}}
    void tick(String hostKey){
        if(closed)return;
        boolean pending=false;
        for(Bot b:new ArrayList<>(bots)){
            if(!b.joined){
                pending=true;
                try{
                    if(b.discovery!=null&&b.discovery.phase.equals("error")){fail(b,"Bot connection failed: "+b.discovery.message);continue;}
                    if(b.bound){
                        File log=new File(b.root,"host.log");
                        if(log.isFile())try(RandomAccessFile in=new RandomAccessFile(log,"r")){
                            long offset=Math.max(0,in.length()-65536);in.seek(offset);byte[] tail=new byte[(int)(in.length()-offset)];in.readFully(tail);
                            if(new String(tail,StandardCharsets.UTF_8).contains("OPEN_RUNE_BOT_JOINED")){b.joined=true;message="Bot joined.";}
                        }
                    }
                    if(!b.joined&&SystemClock.elapsedRealtime()-b.began>60000)fail(b,"A bot timed out joining. Check capacity and try again.");
                }catch(IOException e){fail(b,"Could not read bot status.");}
            }
        }
        if(queued==0||pending||(publicMatch&&hostKey.isEmpty()))return;
        try{
            JSONObject status=new JSONObject(new String(Files.readAllBytes(new File(session,"invitations/discovery-status.json").toPath()),StandardCharsets.UTF_8));
            if(System.currentTimeMillis()-status.getLong("updated")>5000)return;
            HashSet<Integer> used=new HashSet<>();used.add(1);JSONArray occupied=status.getJSONArray("occupied");for(int i=0;i<occupied.length();i++)used.add(occupied.getInt(i));for(Bot b:bots)if(b.slot>0)used.add(b.slot);
            int slot=capacity;while(slot>1&&used.contains(slot))slot--;
            if(slot<=1){queued=0;message="Match is full. Wait for a free slot or host with a higher player limit.";return;}
            // Do not reuse a component while its previous engine is still
            // tearing down; its process globals cannot start a second match.
            HashSet<String> running=new HashSet<>();
            android.app.ActivityManager manager=(android.app.ActivityManager)context.getSystemService(Context.ACTIVITY_SERVICE);
            java.util.List<android.app.ActivityManager.RunningAppProcessInfo> processes=manager.getRunningAppProcesses();
            if(processes!=null)for(android.app.ActivityManager.RunningAppProcessInfo process:processes)running.add(process.processName);
            int component=-1;
            for(int i=0;i<15;i++){
                final int index=i;
                if(bots.stream().noneMatch(b->b.component==index)&&(retired[i]==0||(SystemClock.elapsedRealtime()-retired[i]>3000&&!running.contains(context.getPackageName()+":multiplayer_bot"+(i+1))))){component=i;break;}
            }
            if(component<0)return;
            queued--;final Bot bot=new Bot(component);bots.add(bot);
            try{
                if(publicMatch){
                    bot.discovery=new RuneDiscovery(context,null,"",new RuneDiscovery.Listener(){
                        public void changed(){}
                        public void joined(File invitation,String key){
                            if(bot.removed||closed)return;
                            try{String[] lines=new String(Files.readAllBytes(invitation.toPath()),StandardCharsets.UTF_8).trim().split("\\s+");bot.slot=Integer.parseInt(lines[4]);bot.bind(invitation);}
                            catch(Exception e){fail(bot,"Could not start bot: "+e.getMessage());}
                        }
                    });bot.discovery.command("join",hostKey);
                }else{bot.slot=slot;bot.bind(new File(session,"invitations/player"+slot+".rune-invite"));}
            }catch(Exception|LinkageError e){fail(bot,"Could not start bot: "+e.getMessage());}
        }catch(Exception e){message="Waiting for host player slots…";}
    }
    public void close(){closed=true;queued=0;for(Bot bot:bots)bot.close();bots.clear();}
}
