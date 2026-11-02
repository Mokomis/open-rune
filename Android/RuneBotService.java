package org.runeclassic.port;

import android.app.Service;
import android.content.Intent;
import android.os.*;
import java.io.File;

/** One headless engine per bound process; the foreground host owns its lifetime. */
public class RuneBotService extends Service {
    private static native int nativeRun(String files,String session,String[] args);
    private boolean started;
    @Override public IBinder onBind(Intent intent){
        if(started)return null;started=true;
        String path=intent.getStringExtra("directory"),invite=intent.getStringExtra("invitation");
        File root=new File(path==null?"":path);
        try{
            String allowed=new File(getFilesDir(),"multiplayer/host").getCanonicalPath()+"/";
            if(!root.getCanonicalPath().startsWith(allowed)||!new File(invite).getCanonicalPath().startsWith(getFilesDir().getCanonicalPath()+"/"))throw new IllegalArgumentException("Invalid bot files");
            root.mkdirs();new File(root,"saves").mkdirs();
            for(String library:new String[]{"SDL2","openal","SurrealVideo","runeclassic"})System.loadLibrary(library);
            String[] args={"--direct","--no-desktop-menu","--rune-net-bot","--rune-net-seconds=0","--rune-join="+invite,"--save-directory="+new File(root,"saves"),new File(getFilesDir(),"game").getAbsolutePath()};
            new Thread(()->{nativeRun(getFilesDir().getAbsolutePath(),root.getAbsolutePath(),args);android.os.Process.killProcess(android.os.Process.myPid());},"RuneBot").start();
            return new Binder();
        }catch(Exception|LinkageError e){android.util.Log.e("RuneBot","Could not start bot",e);new Handler(Looper.getMainLooper()).post(()->android.os.Process.killProcess(android.os.Process.myPid()));return null;}
    }
    @Override public void onDestroy(){super.onDestroy();android.os.Process.killProcess(android.os.Process.myPid());}
    public static final class Bot1 extends RuneBotService {}
    public static final class Bot2 extends RuneBotService {}
    public static final class Bot3 extends RuneBotService {}
    public static final class Bot4 extends RuneBotService {}
    public static final class Bot5 extends RuneBotService {}
    public static final class Bot6 extends RuneBotService {}
    public static final class Bot7 extends RuneBotService {}
    public static final class Bot8 extends RuneBotService {}
    public static final class Bot9 extends RuneBotService {}
    public static final class Bot10 extends RuneBotService {}
    public static final class Bot11 extends RuneBotService {}
    public static final class Bot12 extends RuneBotService {}
    public static final class Bot13 extends RuneBotService {}
    public static final class Bot14 extends RuneBotService {}
    public static final class Bot15 extends RuneBotService {}
}
