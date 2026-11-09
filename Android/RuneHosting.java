package org.runeclassic.port;

import android.app.Activity;
import android.content.*;
import android.net.Uri;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import org.json.JSONObject;

/** Activity-side hosting state. All server work is owned by RuneHostService. */
final class RuneHosting {
    interface Listener {void join(File invitation);void changed(String message);}
    final Activity activity;
    final Listener listener;
    final File game;
    File session;
    String state="idle",message="",address="";
    int bots=0,players=4;
    String botMessage="";
    boolean autoJoin,stopRequested,discoverable;
    String discoveryStatus="";
    long started,lastPoll;
    RuneHosting(Activity activity,Listener listener){
        this.activity=activity;this.listener=listener;game=new File(activity.getFilesDir(),"game");
        String id=activity.getSharedPreferences("multiplayer",0).getString("host","");
        if(id.matches("[0-9a-f-]{36}")){
            session=new File(activity.getFilesDir(),"multiplayer/host/"+id);readState();
            try{JSONObject info=new JSONObject(new String(Files.readAllBytes(new File(session,"setup.json").toPath()),StandardCharsets.UTF_8));players=info.getInt("players");address=info.getString("address");discoverable=info.optBoolean("discoverable");}catch(Exception ignored){}
        }
    }
    boolean active(){return state.equals("starting")||state.equals("ready")||state.equals("stopping");}
    boolean ready(){return state.equals("ready");}
    ArrayList<String> addresses(){
        TreeSet<String> lan=new TreeSet<>(),other=new TreeSet<>();
        android.net.ConnectivityManager manager=activity.getSystemService(android.net.ConnectivityManager.class);
        // Only advertise networks Android actually manages. An UP virtual
        // interface can remain present with no route to any other device.
        if(manager!=null)for(android.net.Network network:manager.getAllNetworks()){
            android.net.NetworkCapabilities caps=manager.getNetworkCapabilities(network);
            android.net.LinkProperties link=manager.getLinkProperties(network);
            if(caps==null||link==null)continue;
            boolean physical=caps.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI)||caps.hasTransport(android.net.NetworkCapabilities.TRANSPORT_ETHERNET);
            if(!physical&&!caps.hasTransport(android.net.NetworkCapabilities.TRANSPORT_VPN))continue;
            for(android.net.LinkAddress local:link.getLinkAddresses()){
                InetAddress ip=local.getAddress();
                if(ip instanceof Inet4Address&&!ip.isLoopbackAddress()&&!ip.isLinkLocalAddress())(physical?lan:other).add(ip.getHostAddress());
            }
        }
        ArrayList<String> out=new ArrayList<>(lan);other.removeAll(lan);out.addAll(other);return out;
    }
    void start(RuneHostConfig config,String address){
        if(active())throw new IllegalStateException("A match is already hosted on this device.");
        config.validate(game);
        if(!config.discoverable&&!addresses().contains(address))throw new IllegalArgumentException("Connect to Wi-Fi or a local network before hosting.");
        if(config.discoverable)address="127.0.0.1";
        File next=new File(activity.getFilesDir(),"multiplayer/host/"+UUID.randomUUID());
        if(!next.mkdirs())throw new IllegalStateException("Could not create a private host session.");
        try{
            JSONObject info=new JSONObject();info.put("discoverable",config.discoverable);info.put("players",config.players);info.put("address",address);info.put("mode",config.mode);info.put("maps",String.join(",",config.maps));
            Files.write(new File(next,"setup.json").toPath(),info.toString().getBytes(StandardCharsets.UTF_8));
            Intent intent=new Intent(activity,RuneHostService.class).putExtra("session",next.getName()).putExtra("address",address)
                .putExtra("matchName",config.matchName).putExtra("discoverable",config.discoverable).putExtra("mode",config.mode).putExtra("players",config.players).putExtra("teams",config.teams).putExtra("arenaSize",config.arenaSize)
                .putExtra("laws",config.laws).putExtra("physics",config.physics).putExtra("normalBloodlust",config.normalBloodlust).putExtra("bots",config.bots).putExtra("score",config.score).putExtra("minutes",config.minutes).putExtra("limbs",config.limbs).putExtra("maps",config.maps.toArray(new String[0]));
            activity.startForegroundService(intent);
        }catch(Exception e){throw new IllegalStateException("Could not start hosting: "+e.getMessage(),e);}
        session=next;this.address=address;discoverable=config.discoverable;players=config.players;state="starting";message="Preparing the match…";autoJoin=true;stopRequested=false;started=System.currentTimeMillis();
        activity.getSharedPreferences("multiplayer",0).edit().putString("host",next.getName()).apply();listener.changed(message);
    }
    void poll(){
        long now=System.currentTimeMillis();if(session==null||now-lastPoll<500)return;lastPoll=now;
        String before=state,previousMessage=message,previousBotMessage=botMessage;int previousBots=bots;readState();
        if(ready()&&!discoverable&&!addresses().contains(address))message="Host network disconnected. Reconnect to the same network or stop hosting.";
        if(!before.equals(state)||!previousMessage.equals(message)||previousBots!=bots||!previousBotMessage.equals(botMessage))listener.changed(botMessage.isEmpty()?message:botMessage);
        if(autoJoin&&ready()){autoJoin=false;listener.join(new File(session,"invitations/player1.rune-invite"));}
    }
    private void readState(){
        if(session==null)return;
        try{
            JSONObject status=new JSONObject(new String(Files.readAllBytes(new File(session,"state.json").toPath()),StandardCharsets.UTF_8));
            bots=status.optInt("bots",0);botMessage=status.optString("botMessage","");state=status.getString("state");message=status.getString("message");discoveryStatus=status.optString("discovery","");
            if(active()&&System.currentTimeMillis()-status.getLong("heartbeat")>15000){state="error";message="The host stopped responding. Start a new match.";autoJoin=false;}
            if(stopRequested&&(state.equals("starting")||state.equals("ready"))){state="stopping";message="Stopping the match…";}
        }catch(Exception e){
            if(System.currentTimeMillis()-started>15000){state="error";message="The host did not start. Try hosting again.";autoJoin=false;}
        }
    }
    void stop(){
        if(!active()||stopRequested)return;autoJoin=false;stopRequested=true;state="stopping";message="Stopping the match…";
        try{activity.startService(new Intent(activity,RuneHostService.class).setAction(RuneHostService.STOP));}
        catch(IllegalStateException e){activity.stopService(new Intent(activity,RuneHostService.class));state="stopped";message="Hosting stopped";}
        listener.changed(message);
    }
    void changeBot(boolean add){
        if(!ready()||stopRequested)throw new IllegalStateException("Wait until your host is ready.");
        activity.startService(new Intent(activity,RuneHostService.class).setAction(add?RuneHostService.ADD_BOT:RuneHostService.REMOVE_BOT).putExtra("session",session.getName()));
    }
    void rejoin(){if(ready())listener.join(new File(session,"invitations/player1.rune-invite"));}
    File invitation(int slot){
        if(discoverable)throw new IllegalStateException("Public matches assign slots through Find games.");
        if(!ready()||slot<2||slot>players)throw new IllegalStateException("The match is not ready for invitations.");
        File file=new File(session,"invitations/player"+slot+".rune-invite");
        if(!file.isFile())throw new IllegalStateException("This invitation is unavailable.");return file;
    }
    void share(int slot){
        File file=invitation(slot);
        Uri uri=Uri.parse("content://org.runeclassic.port.invitations/"+session.getName()+"/"+file.getName());
        Intent send=new Intent(Intent.ACTION_SEND).setType("application/octet-stream").putExtra(Intent.EXTRA_STREAM,uri)
            .putExtra(Intent.EXTRA_SUBJECT,"Open Rune · player "+slot)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        send.setClipData(ClipData.newRawUri("Open Rune invitation",uri));
        activity.startActivity(Intent.createChooser(send,"Invite player "+slot));
    }
}
