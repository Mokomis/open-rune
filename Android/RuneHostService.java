package org.runeclassic.port;

import android.app.*;
import android.content.*;
import android.os.*;
import android.util.AtomicFile;
import java.io.*;
import org.json.JSONObject;

/** The engine has process globals: the authoritative world must have its own process. */
public final class RuneHostService extends Service {
    static final String STOP="org.runeclassic.port.STOP_HOST";
    private static native int nativeRun(String files,String session,String[] args);
    private static native void nativeStop();
    private static native boolean nativeReady();
    private static native String nativeError();
    private final Handler handler=new Handler(Looper.getMainLooper());
    private File session;
    private RuneDiscovery discovery;
    private RuneHostBots bots;
    static final String ADD_BOT="org.runeclassic.port.ADD_BOT",REMOVE_BOT="org.runeclassic.port.REMOVE_BOT";
    private String matchName="Open Rune match";
    private boolean publicMatch,discoveryAttempted;
    private boolean loaded,started,stopping,finished;
    private String state="starting",message="Preparing the match…";
    private PowerManager.WakeLock wake;
    @Override public IBinder onBind(Intent intent){return null;}
    private void publish(){
        if(session==null)return;
        AtomicFile file=new AtomicFile(new File(session,"state.json"));FileOutputStream out=null;
        try{
            JSONObject json=new JSONObject();json.put("state",state);json.put("message",message);json.put("heartbeat",System.currentTimeMillis());
            json.put("discovery",discovery==null?"":discovery.message);json.put("bots",bots==null?0:bots.count());json.put("botMessage",bots==null?"":bots.message);
            out=file.startWrite();out.write(json.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));file.finishWrite(out);
        }catch(Exception e){if(out!=null)file.failWrite(out);android.util.Log.e("RuneHost","Cannot save host status",e);}
    }
    private final Runnable pulse=new Runnable(){public void run(){
        if(finished)return;
        if(loaded&&!stopping&&nativeReady()){state="ready";message="Match ready";}
        if(state.equals("ready")&&publicMatch&&!discoveryAttempted){
            discoveryAttempted=true;
            try{discovery=new RuneDiscovery(RuneHostService.this,new File(session,"invitations"),matchName,new RuneDiscovery.Listener(){
                public void changed(){publish();}public void joined(File file,String key){}
            });}catch(Exception|LinkageError e){message="Match ready, but discovery could not start: "+e.getMessage();}
        }
        if(state.equals("ready")&&bots!=null)bots.tick(discovery==null?"":discovery.key);
        publish();handler.postDelayed(this,1000);
    }};
    @Override public int onStartCommand(Intent intent,int flags,int startId){
        if(intent==null){stopSelf();return START_NOT_STICKY;}
        if(STOP.equals(intent.getAction())){stopHost();return START_NOT_STICKY;}
        if(ADD_BOT.equals(intent.getAction())||REMOVE_BOT.equals(intent.getAction())){
            if(started&&!stopping&&state.equals("ready")&&bots!=null&&session.getName().equals(intent.getStringExtra("session"))){if(ADD_BOT.equals(intent.getAction()))bots.add();else bots.remove();publish();}
            if(!started)stopSelf();return START_NOT_STICKY;
        }
        if(started)return START_NOT_STICKY;
        started=true;
        NotificationManager notifications=getSystemService(NotificationManager.class);
        notifications.createNotificationChannel(new NotificationChannel("multiplayer-host","Multiplayer hosting",NotificationManager.IMPORTANCE_LOW));
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP|Intent.FLAG_ACTIVITY_CLEAR_TOP),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent stop=PendingIntent.getService(this,1,new Intent(this,RuneHostService.class).setAction(STOP),PendingIntent.FLAG_IMMUTABLE);
        startForeground(73,new Notification.Builder(this,"multiplayer-host").setSmallIcon(getApplicationInfo().icon)
            .setContentTitle("Open Rune · hosting").setContentText("Private match is running on this device")
            .setContentIntent(open).addAction(new Notification.Action.Builder(null,"Stop match",stop).build()).setOngoing(true).build());
        try {
            String id=intent.getStringExtra("session");
            if(id==null||!id.matches("[0-9a-f-]{36}"))throw new IllegalArgumentException("Invalid host session.");
            session=new File(getFilesDir(),"multiplayer/host/"+id);
            if(!session.isDirectory())throw new IllegalArgumentException("Host session is missing.");
            RuneHostConfig config=new RuneHostConfig();config.mode=intent.getStringExtra("mode");
            config.normalBloodlust=intent.getBooleanExtra("normalBloodlust",false);config.bots=intent.getIntExtra("bots",0);config.players=intent.getIntExtra("players",4);config.teams=intent.getIntExtra("teams",2);config.arenaSize=intent.getIntExtra("arenaSize",1);
            config.score=intent.getIntExtra("score",20);config.minutes=intent.getIntExtra("minutes",10);config.limbs=intent.getBooleanExtra("limbs",false);
            String[] maps=intent.getStringArrayExtra("maps");if(maps!=null)java.util.Collections.addAll(config.maps,maps);
            matchName=config.matchName=intent.getStringExtra("matchName");if(matchName==null)matchName=config.matchName="Open Rune match";
            publicMatch=config.discoverable=intent.getBooleanExtra("discoverable",false);
            String address=intent.getStringExtra("address");
            if(address==null||!address.matches("[0-9.]{7,15}"))throw new IllegalArgumentException("No local network address.");
            final String[] args=config.arguments(new File(getFilesDir(),"game"),session,address);
            for(String library:new String[]{"SDL2","openal","SurrealVideo","runeclassic"})System.loadLibrary(library);
            loaded=true;bots=new RuneHostBots(this,session,config.players,config.bots,publicMatch);
            wake=((PowerManager)getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"OpenRune:MultiplayerHost");wake.acquire();
            handler.post(pulse);
            new Thread(()->{
                int code=nativeRun(getFilesDir().getAbsolutePath(),session.getAbsolutePath(),args);
                String error=nativeError();
                handler.post(()->finishHost(code==0?"stopped":"error",code==0?"Hosting stopped":error.isEmpty()?"Could not start the match. Check game files and selected maps.":error));
            },"RuneHost").start();
        }catch(Exception|LinkageError error){finishHost("error",error.getMessage()==null?"Hosting could not start.":error.getMessage());}
        return START_NOT_STICKY;
    }
    private void stopHost(){
        if(stopping)return;stopping=true;state="stopping";message="Stopping the match…";publish();
        if(bots!=null)bots.close();
        if(discovery!=null)discovery.close();
        if(loaded)nativeStop();else finishHost("stopped","Hosting stopped");
        // A hung load must not leave an unkillable server behind.
        handler.postDelayed(()->finishHost("stopped","Hosting stopped"),10000);
    }
    private void finishHost(String result,String detail){
        if(finished)return;if(bots!=null)bots.close();if(discovery!=null)discovery.close();finished=true;state=result;message=detail;publish();handler.removeCallbacks(pulse);
        if(wake!=null&&wake.isHeld())wake.release();stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();
    }
    @Override public void onDestroy(){
        handler.removeCallbacksAndMessages(null);if(bots!=null)bots.close();if(discovery!=null)discovery.close();
        if(!finished){if(loaded)nativeStop();state="stopped";message="Hosting stopped";publish();}
        if(wake!=null&&wake.isHeld())wake.release();super.onDestroy();
        // Never reuse the engine's process globals for a second match.
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
