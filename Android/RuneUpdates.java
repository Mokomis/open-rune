package org.runeclassic.port;
import android.content.*;
import android.net.Uri;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import javax.net.ssl.HttpsURLConnection;
import org.json.*;
/** Release metadata only. Never downloads APKs, installs code, or carries GitHub credentials. */
final class RuneUpdates {
    static final String BASE="https://github.com/"+OpenRuneBuild.REPOSITORY+"/releases";
    static final class Result {
        String message="Updates have not been checked.",version="",notes="",url=BASE;
        boolean busy,available;
    }
    static volatile Result current=new Result();
    private static boolean started;private static long checked;
    static String identity(){return "Open Rune "+OpenRuneBuild.VERSION+" (build "+OpenRuneBuild.BUILD+")\nNetwork "+OpenRuneBuild.PROTOCOL+" · Rules "+OpenRuneBuild.RULES;}
    static boolean automatic(Context c){return c.getSharedPreferences("releaseUpdates",0).getBoolean("automatic",true);}
    static void setAutomatic(Context c,boolean enabled){c.getSharedPreferences("releaseUpdates",0).edit().putBoolean("automatic",enabled).apply();}
    static synchronized void check(Context c,boolean manual){
        if(!manual&&!automatic(c))return;
        long now=android.os.SystemClock.elapsedRealtime();
        if(current.busy||(!manual&&started)||(checked!=0&&now-checked<60000))return;
        started=true;checked=now;Result busy=new Result();busy.busy=true;busy.message="Checking published releases…";current=busy;
        new Thread(()->{current=fetch();},"OpenRune-ReleaseCheck").start();
    }
    static Result inspect(int status,String body){
        Result r=new Result();
        if(status==404){r.message="No public stable release is available. The repository may still be private.";return r;}
        if(status==403||status==429){r.message="GitHub has limited update checks. Try again later.";return r;}
        if(status!=200){r.message="Could not check releases. Check your connection and try again.";return r;}
        try{
            if(body.getBytes(StandardCharsets.UTF_8).length>262144)throw new IOException("Oversized release");
            JSONObject j=new JSONObject(body);
            if(!(j.get("draft") instanceof Boolean)||!(j.get("prerelease") instanceof Boolean)||j.getBoolean("draft")||j.getBoolean("prerelease"))throw new IOException("Not stable");
            if(!(j.get("tag_name") instanceof String)||!(j.get("html_url") instanceof String)||!(j.get("body") instanceof String)&&!j.isNull("body"))throw new IOException("Missing fields");
            String tag=j.getString("tag_name");RuneReleaseVersion remote=RuneReleaseVersion.parse(tag);
            if(remote.pre.length!=0||!j.getString("html_url").equals(BASE+"/tag/"+tag))throw new IOException("Invalid release");
            r.version=tag;r.url=j.getString("html_url");String notes=j.isNull("body")?"":j.getString("body");
            r.notes=notes.substring(0,Math.min(6000,notes.length())).replaceAll("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F]"," ");
            r.available=remote.compareTo(RuneReleaseVersion.parse(OpenRuneBuild.VERSION))>0;
            r.message=r.available?"Update available: "+tag:"No newer stable release. Installed: "+OpenRuneBuild.VERSION;
        }catch(Exception e){r=new Result();r.message="GitHub returned an invalid release response. Nothing was downloaded.";}
        return r;
    }
    private static Result fetch(){
        HttpsURLConnection connection=null;
        try{
            connection=(HttpsURLConnection)new URL("https://api.github.com/repos/"+OpenRuneBuild.REPOSITORY+"/releases/latest").openConnection();
            connection.setConnectTimeout(5000);connection.setReadTimeout(4000);connection.setInstanceFollowRedirects(false);
            connection.setRequestProperty("Accept","application/vnd.github+json");connection.setRequestProperty("User-Agent","OpenRune-ReleaseChecker/1");
            long deadline=android.os.SystemClock.elapsedRealtime()+12000;int code=connection.getResponseCode();if(code!=200)return inspect(code,"");
            try(InputStream in=connection.getInputStream();ByteArrayOutputStream out=new ByteArrayOutputStream()){
                byte[] buffer=new byte[4096];int count;
                while((count=in.read(buffer))!=-1){if(out.size()+count>262144||android.os.SystemClock.elapsedRealtime()>deadline)throw new IOException("Response limit");out.write(buffer,0,count);}
                return inspect(code,new String(out.toByteArray(),StandardCharsets.UTF_8));
            }
        }catch(Exception e){return inspect(0,"");}finally{if(connection!=null)connection.disconnect();}
    }
    static boolean open(Context c){
        String url=current.url;if(!url.equals(BASE)){String prefix=BASE+"/tag/";if(!url.startsWith(prefix))return false;try{RuneReleaseVersion.parse(url.substring(prefix.length()));}catch(Exception e){return false;}}
        try{c.startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse(url)));return true;}catch(ActivityNotFoundException e){return false;}
    }
}
