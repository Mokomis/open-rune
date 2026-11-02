package org.runeclassic.port;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.AtomicFile;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.UUID;
import org.json.*;
import to.holepunch.bare.kit.Worklet;

/** App-local HyperDHT worklet; only bounded metadata crosses onto the UI thread. */
final class RuneDiscovery {
    interface Listener { void changed(); void joined(File invitation,String key); }
    private final Context context;
    private final Listener listener;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final File control;
    private Worklet worklet;
    private long commandId,joinedId;
    private String lastFingerprint="";
    boolean closed;
    String phase="ready",message="Ready",key="";
    JSONArray rows=new JSONArray();
    RuneDiscovery(Context context,File hostDirectory,String matchName,Listener listener) throws Exception {
        this.context=context;this.listener=listener;
        control=new File(context.getFilesDir(),"multiplayer/discovery/"+UUID.randomUUID());
        if(!control.mkdirs())throw new IOException("Could not prepare game discovery.");
        JSONObject config=new JSONObject();config.put("control",control.getAbsolutePath());
        if(hostDirectory!=null){config.put("directory",hostDirectory.getAbsolutePath());config.put("name",matchName);}
        write(new File(control,"config.json"),config.toString().getBytes(StandardCharsets.UTF_8));
        Worklet.Options options=new Worklet.Options().memoryLimit(64*1024*1024);
        worklet=new Worklet(options);
        try(InputStream source=context.getAssets().open("discovery.bundle")){
            worklet.start("/discovery.bundle",source,new String[]{new File(control,"config.json").getAbsolutePath()});
        }
        handler.post(poll);
    }
    private static void write(File destination,byte[] bytes) throws IOException {
        AtomicFile file=new AtomicFile(destination);FileOutputStream out=null;
        try{out=file.startWrite();out.write(bytes);file.finishWrite(out);}catch(IOException e){if(out!=null)file.failWrite(out);throw e;}
    }
    private final Runnable poll=new Runnable(){public void run(){
        if(closed)return;
        try{
            File file=new File(control,"state.json");
            if(file.isFile()){
                if(file.length()>65536)throw new IOException("Discovery response is too large.");
                JSONObject value=new JSONObject(new String(Files.readAllBytes(file.toPath()),StandardCharsets.UTF_8));
                phase=value.optString("phase","error");message=value.optString("message","");key=value.optString("key","");
                rows=value.optJSONArray("rows");if(rows==null)rows=new JSONArray();
                if(System.currentTimeMillis()-value.optLong("updated")>10000){phase="error";message="Discovery stopped responding. Close this page and retry.";}
                long id=value.optLong("joinId");
                if(phase.equals("joined")&&id>joinedId){
                    joinedId=id;File invite=new File(control,"joined.rune-invite");
                    RuneInvitation.normalize(Files.readAllBytes(invite.toPath()));
                    listener.joined(invite,key);
                }
                String fingerprint=phase+message+rows.toString();
                if(!fingerprint.equals(lastFingerprint)){lastFingerprint=fingerprint;listener.changed();}
            }
        }catch(Exception e){phase="error";message=e.getMessage();listener.changed();}
        handler.postDelayed(this,250);
    }};
    boolean busy(){return phase.equals("searching")||phase.equals("joining");}
    void command(String action,String hostKey){
        if(closed)return;
        try{
            JSONObject value=new JSONObject();value.put("id",++commandId);value.put("action",action);if(hostKey!=null)value.put("key",hostKey);
            write(new File(control,"command.json"),value.toString().getBytes(StandardCharsets.UTF_8));
            if(action.equals("browse")){rows=new JSONArray();phase="searching";message="Searching for games…";}
            if(action.equals("join")){phase="joining";message="Connecting to the host…";}
            listener.changed();
        }catch(Exception e){phase="error";message="Could not contact discovery.";listener.changed();}
    }
    void close(){
        if(closed)return;command("stop",null);closed=true;handler.removeCallbacks(poll);
        final Worklet old=worklet;worklet=null;
        // Give graceful withdrawal time without blocking Android's main thread.
        handler.postDelayed(()->new Thread(()->{try{old.terminate();}catch(Exception ignored){}},"DiscoveryClose").start(),2000);
    }
}
