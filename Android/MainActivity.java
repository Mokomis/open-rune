package org.runeclassic.port;

import org.libsdl.app.SDLActivity;
import android.os.Bundle;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashSet;
import android.view.KeyEvent;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.view.SurfaceView;
import android.view.SurfaceHolder;
import android.view.Surface;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.RelativeLayout;
import android.widget.TextView;
import java.util.Locale;

public class MainActivity extends SDLActivity implements RuneMenuView.Host, RuneHudView.Host, RuneController.Host, RuneTouchView.Host {
    private static native String nativeCharacterRoster();
    public String characterRoster(){return nativeCharacterRoster();}
    public int characterPreview(int character,int skin,int[] pixels,float yaw){return nativeCharacterPreview(character,skin,pixels,yaw);}
    private static native int nativeCharacterPreview(int character,int skin,int[] pixels,float yaw);
    public void characterPreviewRotation(int character,int skin,float yaw){nativeCharacterPreviewRotation(character,skin,yaw);}
    private static native void nativeCharacterPreviewRotation(int character,int skin,float yaw);
    private static native boolean nativeMultiplayerActive();
    private static native boolean nativeMultiplayerWaiting();
    private boolean networkWasWaiting;
    private File lastMultiplayerInvite;
    private static native boolean nativeMultiplayerJoin(String file);
    private static native boolean nativeSetPlayerName(String name);
    private static native String nativeMultiplayerStatus();
    private static native void nativeMultiplayerAction(int action);
    private boolean wasMultiplayer, respawning, networkWasDead, bloodlustTouchScheduled,slamTouchScheduled;
    private RuneHosting hosting;
    private RuneDiscovery discovery;
    private String discoveryKey(){return getSharedPreferences("multiplayer",0).getString("discoveryKey","");}
    private void forgetDiscovery(){getSharedPreferences("multiplayer",0).edit().remove("discoveryKey").apply();if(discovery!=null)discovery.command("leave",null);}
    public RuneDiscovery discovery(){
        if(discovery==null||discovery.closed)try{
            discovery=new RuneDiscovery(this,null,null,new RuneDiscovery.Listener(){
                public void changed(){if(menu!=null&&menuOpen&&menu.screen.equals("findgames"))menu.showFindGames(false);}
                public void joined(File invite,String key){getSharedPreferences("multiplayer",0).edit().putString("discoveryKey",key).apply();joinInvitation(invite);}
            });
        }catch(Exception|LinkageError e){if(menu!=null)menu.setNotice("Game discovery could not start: "+e.getMessage());}
        return discovery;
    }
    public void joinDiscovered(String key){if(options.playerName().isEmpty()){menu.playerProfile(()->joinDiscovered(key));return;}nativeSetPlayerName(options.playerName());RuneDiscovery d=discovery();if(d!=null)d.command("join",key);}

    private File exportInvitation;
    public RuneHosting hosting(){
        if(hosting==null)hosting=new RuneHosting(this,new RuneHosting.Listener(){
            public void join(File file){forgetDiscovery();joinInvitation(file);if(multiplayer()){openMenu();menu.showInvitations();}}
            public void changed(String message){if(menu!=null){menu.setNotice(message);if(menuOpen&&(menu.screen.equals("multiplayer")||menu.screen.equals("invitations")))menu.showMultiplayer();}}
        });
        return hosting;
    }
    private File rejoinInvitation(){return lastMultiplayerInvite!=null&&lastMultiplayerInvite.isFile()?lastMultiplayerInvite:new File(getFilesDir(),"multiplayer/selected.rune-invite");}
    public boolean hasInvitation(){return !discoveryKey().isEmpty()||rejoinInvitation().isFile();}
    public void rejoinMultiplayer(){if(!discoveryKey().isEmpty()){menu.showFindGames(false);joinDiscovered(discoveryKey());}else joinInvitation(rejoinInvitation());}
    public boolean multiplayerWaiting(){return nativeMultiplayerWaiting();}
    public void saveInvitation(int slot){
        try{
            exportInvitation=hosting().invitation(slot);
            android.content.Intent intent=new android.content.Intent(android.content.Intent.ACTION_CREATE_DOCUMENT)
                .addCategory(android.content.Intent.CATEGORY_OPENABLE).setType("application/octet-stream")
                .putExtra(android.content.Intent.EXTRA_TITLE,exportInvitation.getName());startActivityForResult(intent,703);
        }catch(Exception e){menu.setNotice(e.getMessage());}
    }
    public boolean multiplayer(){return nativeMultiplayerActive();}
    public String multiplayerStatus(){
        String value=nativeMultiplayerStatus();
        if(!discoveryKey().isEmpty())value=value.replace("Ask for a new invitation.","Use Find games to look for another match.").replace("Check the invitation and host.","Check that the host is still available.");
        return value;
    }
    public void leaveMultiplayer(){releaseKeys();nativeMultiplayerAction(1);if(discovery!=null)menu.postDelayed(()->discovery.command("leave",null),1000);if(hosting!=null)hosting.stop();}
    public void respawnMultiplayer(){nativeMultiplayerAction(2);respawning=true;menu.setNotice("Respawning when ready…");}
    public void chooseInvitation(){
        android.content.Intent picker=new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);
        picker.addCategory(android.content.Intent.CATEGORY_OPENABLE);picker.setType("*/*");
        try{startActivityForResult(picker,702);}catch(android.content.ActivityNotFoundException e){menu.setNotice("No document picker is available on this device.");}
    }
    private String textureInstallStatus = "";
    private boolean textureInstallActive;
    private int pendingTextureQuality;

    @Override public void selectTextureArtwork(int quality) {
        File game = new File(getFilesDir(), "game");
        if (quality == 0 || RuneTexturePack.ready(game)) {
            pendingTextureQuality = 0;
            options.set("texturePack", quality);
            return;
        }
        if (textureInstallActive || pendingTextureQuality > 0) {
            pendingTextureQuality = quality;
            return;
        }
        menu.confirm("Download WIP textures?",
            "Download about 4.25 GB on an unmetered connection. The unfinished pack includes both 2× and 4×. Original stays active while it installs.",
            "Download", () -> {
                pendingTextureQuality = quality;
                mLayout.removeCallbacks(textureInstallCheck);
                textureInstallCheck.run();
            });
    }

    private final Runnable textureInstallCheck = new Runnable() {
        @Override public void run() {
            if (pendingTextureQuality == 0 || getIntent().hasExtra("saveTest")) return;
            File game = new File(getFilesDir(), "game");
            if (RuneTexturePack.ready(game)) {
                int quality = pendingTextureQuality;
                pendingTextureQuality = 0;
                options.set("texturePack", quality);
                if (menu != null) menu.refresh();
                return;
            }
            if (!textureInstallActive) {
                textureInstallActive = true;
                textureProgress("Preparing the WIP texture download…");
                new Thread(() -> {
                    try {
                        RuneTexturePack.prepare(getApplicationContext(), game, MainActivity.this::textureProgress);
                    } catch (Exception e) {
                        android.util.Log.w("OpenRune", "Texture preparation will retry", e);
                        textureProgress("The WIP texture download will retry.");
                    } finally {
                        runOnUiThread(() -> {
                            textureInstallActive = false;
                            if (RuneTexturePack.ready(game) && pendingTextureQuality != 0) {
                                mLayout.removeCallbacks(textureInstallCheck);
                                textureInstallCheck.run();
                            }
                        });
                    }
                }, "Open Rune texture preparation").start();
            }
            mLayout.postDelayed(this, 120_000);
        }
    };

    private void textureProgress(String value) {
        runOnUiThread(() -> {
            textureInstallStatus = value;
            if (menu != null) menu.updateTextureInstallStatus();
        });
    }
    @Override public String textureInstallStatus() {
        if (!textureInstallStatus.isEmpty()) return textureInstallStatus;
        return RuneTexturePack.ready(new File(getFilesDir(), "game")) ? "" : "Choose WIP 2× or WIP 4× to download.";
    }
    @Override protected void onActivityResult(int request,int result,android.content.Intent data){
        super.onActivityResult(request,result,data);
        if(request==703){
            if(result==RESULT_OK&&data!=null&&data.getData()!=null&&exportInvitation!=null){
                try(java.io.OutputStream out=getContentResolver().openOutputStream(data.getData())){
                    if(out==null)throw new java.io.IOException("Could not save invitation");out.write(java.nio.file.Files.readAllBytes(exportInvitation.toPath()));menu.setNotice("Invitation saved. Send a different slot to each player.");
                }catch(java.io.IOException e){menu.setNotice(e.getMessage());}
            }
            exportInvitation=null;return;
        }
        if(request!=702||result!=RESULT_OK||data==null||data.getData()==null)return;
        try(InputStream input=getContentResolver().openInputStream(data.getData())){
            java.io.ByteArrayOutputStream bytes=new java.io.ByteArrayOutputStream();byte[] buffer=new byte[1025];int n;
            if(input==null)throw new java.io.IOException("Invitation could not be opened");
            while((n=input.read(buffer))>0){if(bytes.size()+n>1024)throw new java.io.IOException("Invitation is too large");bytes.write(buffer,0,n);}
            byte[] normalized=RuneInvitation.normalize(bytes.toByteArray());
            menu.confirm("Join this match?","Unsaved campaign progress will be replaced. Existing saves are kept.","Join",()->{
                try{
                    File dir=new File(getFilesDir(),"multiplayer");dir.mkdirs();File invite=new File(dir,"selected.rune-invite");
                    android.util.AtomicFile file=new android.util.AtomicFile(invite);FileOutputStream output=null;
                    try{output=file.startWrite();output.write(normalized);file.finishWrite(output);}catch(java.io.IOException e){if(output!=null)file.failWrite(output);throw e;}
                    invite.setReadable(false,false);invite.setReadable(true,true);invite.setWritable(false,false);invite.setWritable(true,true);forgetDiscovery();joinInvitation(invite);
                }catch(java.io.IOException e){menu.setNotice(e.getMessage());}
            });
        }catch(java.io.IOException e){menu.setNotice(e.getMessage());}
    }
    private void joinInvitation(File invite){
        if(options.playerName().isEmpty()){menu.playerProfile(()->joinInvitation(invite));return;}
        nativeSetPlayerName(options.playerName());
        releaseKeys();
        if(nativeMultiplayerJoin(invite.getAbsolutePath())){lastMultiplayerInvite=invite;lastResultTime=0;networkWasWaiting=false;menu.setNotice("");wasMultiplayer=true;respawning=false;busySession=false;menu.hasSession=false;updateTouchVisibility();resumeNetwork();}
        else menu.setNotice(multiplayerStatus());
    }
    private void resumeNetwork(){
        nativeMenuSetting(2,options.frameTarget());nativeMenuSetting(3,options.get("pacing"));
        releaseKeys();menuOpen=false;menuLayer.setVisibility(View.GONE);controls.setVisibility(View.VISIBLE);hud.setVisibility(View.VISIBLE);
        nativeMenuSetting(0,0);mSurface.requestFocus();
    }
    private static native void nativeSetEnhanced(boolean enabled);
    private static native float nativeGetViewFov();
    private static native int nativeGetVisualMode();
    private static native float nativeGetEngineFps();
    private static native void nativeSetUpscaleMode(int mode);
    private static native long nativeScalingSnapshot();
    private static native int nativeGetUpscaleMode();
    private static native void nativeSetClarity(int settings);
    private static native int nativeGetClarity();
    private static native void nativeSetWorldFeatures(int settings);
    private static native void nativeSetShadowBudget(int budget);
    private static native void nativeSetTexturePack(int quality);
    static native String nativeTexturePackStatus();
    private static native int nativeGetWorldFeatures();
    private static native boolean nativeCombatReady();
    private static native int nativeBloodlustMoves();
    private static native boolean nativePlayerDead();
    private static native boolean nativePracticeArena();
    private static native int nativeSessionResult();
    private static native void nativeMenuSetting(int id,int value);
    private static native String nativeMenuMetadata();
    private static native int nativeMenuVolume(int kind);
    private static native boolean nativeMenuSession(int action,int slot);
    private static native String nativeHudSnapshot(float[] values);
    private static native void nativeControllerAxes(float x,float y,float yaw,float pitch);
    private RuneController controller;
    private RuneRumble rumble;
    private boolean feedbackEnabled;
    private static native void nativeFeedbackEnabled(boolean enabled,boolean trace);
    private static native long nativeFeedbackPoll();
    private static native long nativeFeedbackPreview();
    private void syncFeedback(){
        boolean enabled=rumble!=null&&controlsVisible&&hasWindowFocus()&&!menuOpen&&!busySession&&nativeCombatReady()&&options.get("rumble")!=0&&options.get("rumbleStrength")>0;
        if(enabled!=feedbackEnabled){feedbackEnabled=enabled;nativeFeedbackEnabled(enabled,getIntent().getBooleanExtra("rumbleProbe",false));if(!enabled&&rumble!=null)rumble.stop();}
    }
    public String rumbleStatus(){return rumble==null?"Checking vibration support…":rumble.status();}
    public void testRumble(){if(rumble!=null)rumble.play(nativeFeedbackPreview(),true);}
    private final HashSet<Integer> controllerKeys=new HashSet<>();
    private RuneHudView hud;
    private final float[] hudValues=new float[12];
    private String hudFps="";
    private RuneSettings options;
    private RuneMenuView menu;
    private FrameLayout menuLayer;
    private RuneTouchView controls;
    private TextView fpsLabel;
    private boolean controlsVisible,menuOpen,initializedAudio,busySession;
    private long lastResultTime;
    private long deathObservedAt;
    public void controllerConnectionChanged(){updateTouchVisibility();}
    private void updateTouchVisibility(){
        if(options!=null&&controls!=null)controls.configure(options.get("touch")!=0&&(controller==null||!controller.connected()),(((multiplayer()?nativeBattleLaws():RuneBattleLaws.campaign(options))&1)!=0));
    }
    public void touchKey(int code,boolean down){key(code,down);if(getIntent().getBooleanExtra("controllerProbe",false))android.util.Log.i("RuneTouch","key="+code+" down="+down);}
    private float touchX,touchY,touchYaw,touchPitch;
    private void publishTouchAxes(){float speed=controllerSensitivity();nativeControllerAxes(touchX,touchY,touchYaw*speed,touchPitch*speed*(controllerInvert()?-1:1));if(getIntent().getBooleanExtra("controllerProbe",false))android.util.Log.i("RuneTouch","axes move="+touchX+","+touchY+" look="+touchYaw+","+touchPitch);}
    public void touchMove(float x,float y){touchX=x;touchY=y;publishTouchAxes();}
    public void touchCamera(float x,float y){touchYaw=x;touchPitch=y;publishTouchAxes();}
    public void touchLook(float x,float y){float speed=new float[]{.25f,.375f,.5f,.65f,.8f}[options.get("sensitivity")];onNativeMouse(0,MotionEvent.ACTION_MOVE,x*speed,y*speed*(options.get("invert")==0?1:-1),true);}
    public void touchMenu(){openMenu();}
    public void editTouch(){releaseKeys();nativeMenuSetting(0,1);menuOpen=true;menuLayer.setVisibility(View.GONE);hud.setVisibility(View.VISIBLE);controls.begin();}
    public void closeTouchEditor(){menuOpen=true;nativeMenuSetting(0,1);controls.setVisibility(View.GONE);hud.setVisibility(View.GONE);menuLayer.setVisibility(View.VISIBLE);menu.requestFocus();}
    private final HashSet<Integer> heldKeys=new HashSet<>();
    private int initialWorldFeatures() {
        return getIntent().hasExtra("worldFeatures")?getIntent().getIntExtra("worldFeatures",1):getPreferences(MODE_PRIVATE).getInt("worldFeatures",1);
    }
    private int initialShadowBudget() {
        return Math.max(1,Math.min(8,getIntent().getIntExtra("shadowLights",options.shadowBudget())));
    }
    private int initialTexturePack() {
        return Math.max(0,Math.min(2,getIntent().getIntExtra("texturePack",getPreferences(MODE_PRIVATE).getInt("menu.texturePack",0))));
    }
    private int initialClarity() {
        if(getIntent().hasExtra("clarity"))return getIntent().getIntExtra("clarity",13);
        return getPreferences(MODE_PRIVATE).getBoolean("clarityEnabled",true)?getPreferences(MODE_PRIVATE).getInt("claritySettings",13):0;
    }
    private boolean initialEnhanced() {
        return getIntent().hasExtra("enhanced")?getIntent().getBooleanExtra("enhanced",false):getPreferences(MODE_PRIVATE).getBoolean("enhanced",false);
    }
    private boolean initialUpscale() {
        return getIntent().hasExtra("upscale")?getIntent().getBooleanExtra("upscale",false):getPreferences(MODE_PRIVATE).getBoolean("upscale",false);
    }
    public RuneSettings settings(){return options;}
    public boolean ready(){return !busySession&&nativeCombatReady();}
    public boolean dead(){return nativePlayerDead();}
    public boolean practiceArena(){return nativePracticeArena();}
    public String metadata(){return nativeMenuMetadata();}
    public void exit(){finish();}
    public boolean session(int action,int slot) {
        releaseKeys();
        if(!nativeMenuSession(action,slot))return false;
        busySession=true;menu.hasSession=true;return true;
    }
    public void editHud() {
        releaseKeys();nativeMenuSetting(0,1);nativeMenuSetting(8,1);menuOpen=true;
        controls.setVisibility(View.GONE);menuLayer.setVisibility(View.GONE);hud.begin(menu.titleMode,menu.art);
    }
    public void closeHudEditor() {
        menuOpen=true;nativeMenuSetting(0,1);nativeMenuSetting(8,options.get("hud"));hud.setVisibility(View.GONE);
        menuLayer.setVisibility(View.VISIBLE);menu.requestFocus();
    }
    private boolean frameBenchmark() {
        return getIntent().hasExtra("saveTest") && getIntent().getIntExtra("frames", 0) > 0
                && getIntent().getBooleanExtra("frameBenchmark", false);
    }

    public void resume() {
        if(!busySession&&dead()){openMenu();return;}
        releaseKeys();menuOpen=false;menuLayer.setVisibility(View.GONE);
        int overlayVisibility = frameBenchmark() ? View.GONE : View.VISIBLE;
        controls.setVisibility(overlayVisibility);
        hud.setVisibility(overlayVisibility);
        nativeMenuSetting(0,0);mSurface.requestFocus();syncFeedback();
    }
    private void openMenu() {
        releaseKeys();menuOpen=true;nativeMenuSetting(0,1);
        controls.setVisibility(View.GONE);hud.setVisibility(View.GONE);menuLayer.setVisibility(View.VISIBLE);menu.open(false);
        menu.setFocusableInTouchMode(true);menu.requestFocus();
    }
    private void releaseKeys(){if(controls!=null)controls.release();if(controller!=null)controller.reset();for(int code:new HashSet<>(heldKeys))key(code,false);feedbackEnabled=false;nativeFeedbackEnabled(false,getIntent().getBooleanExtra("rumbleProbe",false));if(rumble!=null)rumble.stop();}
    public String controllerStatus(){return controller==null?"Checking controller…":controller.status();}
    public boolean controllerMenu(){return menuOpen;}
    public float controllerSensitivity(){return new float[]{.5f,.75f,1f,1.3f,1.6f}[options.get("sensitivity")];}
    public boolean controllerInvert(){return options.get("invert")!=0;}
    public void controllerAxes(float x,float y,float yaw,float pitch){nativeControllerAxes(x,y,yaw,pitch);}
    public void controllerKey(int code,boolean down){
        if(down){if(!menuOpen&&controllerKeys.add(code)&&!heldKeys.contains(code))onNativeKeyDown(code);}
        else if(controllerKeys.remove(code)&&!heldKeys.contains(code))onNativeKeyUp(code);
    }
    public void controllerNavigate(int code){
        menu.controllerKey(new KeyEvent(KeyEvent.ACTION_DOWN,code));
        menu.controllerKey(new KeyEvent(KeyEvent.ACTION_UP,code));
    }
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private void key(int code,boolean down) {
        if(down){if(!menuOpen&&heldKeys.add(code)&&!controllerKeys.contains(code))onNativeKeyDown(code);}
        else if(heldKeys.remove(code)&&!controllerKeys.contains(code))onNativeKeyUp(code);
    }
    private void applySettings(String changed) {
        if(changed.equals("soundRemaster")&&menu!=null)menu.setNotice("Restart Open Rune to apply the sound effects setting.");
        if(changed.equals("water")&&menu!=null)menu.setNotice("Restart Open Rune after switching to or from Crest water.");
        // Comparison is temporary. Never overwrite saved effect selections.
        boolean original=options.comparing;
        nativeSetEnhanced(!original&&options.get("bloom")!=0);
        nativeSetClarity(original?0:options.clarity());
        nativeSetWorldFeatures(original?(options.world()&9):options.world());
        nativeSetShadowBudget(changed.equals("initial")?initialShadowBudget():options.shadowBudget());
        nativeSetTexturePack(original?0:options.get("texturePack"));
        if(changed.equals("initial")||changed.equals("resolution")||changed.equals("scalingMethod")) {
            nativeSetUpscaleMode(gsrMode());
            resizeGameSurface();
        }
        if(changed.equals("initial")||changed.equals("aa"))nativeMenuSetting(1,options.get("aa"));
        if(changed.equals("initial")||changed.equals("fov"))nativeMenuSetting(11,options.get("fov"));
        if(changed.equals("initial")||changed.equals("wideView"))nativeMenuSetting(12,options.get("wideView"));
        if(changed.equals("initial")||changed.equals("fps")||changed.equals("fpsManual"))nativeMenuSetting(2,options.frameTarget());
        if(changed.equals("refresh")||changed.equals("refreshManual"))getIntent().removeExtra("refresh");
        if(changed.equals("initial")||changed.equals("refresh")||changed.equals("refreshManual"))applyRefreshRate();
        if(changed.equals("initial")||changed.equals("pacing"))nativeMenuSetting(3,options.get("pacing"));
        if(changed.equals("initial")||changed.equals("master"))nativeMenuSetting(4,options.get("master"));
        if(changed.equals("music")||(changed.equals("initial")&&getPreferences(MODE_PRIVATE).contains("menu.music")))nativeMenuSetting(5,options.get("music"));
        if(changed.equals("effects")||(changed.equals("initial")&&getPreferences(MODE_PRIVATE).contains("menu.effects")))nativeMenuSetting(6,options.get("effects"));
        nativeMenuSetting(7,options.get("difficulty"));
        nativeSetPlayerName(options.playerName());nativeMenuSetting(27,options.get("playerNames"));
        nativeMenuSetting(22,options.get("networkDiagnostics"));
        nativeMenuSetting(23,options.get("networkSmoothing"));
        nativeMenuSetting(19,options.characterChoice(false));
        nativeMenuSetting(20,options.characterChoice(true));
        float alpha=new float[]{.35f,.5f,.65f,.8f,1f}[options.get("opacity")];
        if(changed.equals("opacity")&&controls!=null)controls.globalOpacity(alpha);
        updateTouchVisibility();
        if(fpsLabel!=null)fpsLabel.setVisibility(View.GONE);
        nativeMenuSetting(8,options.get("hud"));
        nativeMenuSetting(26,RuneBattleLaws.campaign(options));
        nativeMenuSetting(9,1);
        nativeMenuSetting(10,1);
        nativeMenuSetting(13,1);
        nativeMenuSetting(14,1);
        nativeMenuSetting(15,1);nativeMenuSetting(21,1);
        nativeMenuSetting(16,original?0:options.get("fireEffects"));
        nativeMenuSetting(18,original?-2:options.get("snow")-1);
        nativeMenuSetting(24,original?0:options.get("gore"));
        nativeMenuSetting(25,getIntent().hasExtra("physicsMode")?getIntent().getIntExtra("physicsMode",RuneSettings.PHYSICS_DEFAULT):options.get("physics"));
        if(hud!=null){hud.modern=options.get("hud")!=0;hud.canvas.invalidate();}
        if(rumble!=null&&(changed.equals("initial")||changed.startsWith("rumble")))rumble.configure(options.get("rumble")!=0,options.get("rumbleStrength"),options.get("rumbleOutput"));
        syncFeedback();
    }
    static native int nativeBattleLaws();
    public String cameraStatus(){
        float h=nativeGetViewFov();
        float aspect=mSurface!=null&&mSurface.getHeight()>0?(float)mSurface.getWidth()/mSurface.getHeight():4f/3f;
        double v=Math.toDegrees(2*Math.atan(Math.tan(Math.toRadians(h)/2)/aspect));
        return String.format(java.util.Locale.ROOT,"Current camera: %.1f° horizontal · %.1f° vertical",h,v);
    }
    private final Runnable updateStatus=new Runnable(){public void run(){
        if(!controlsVisible||fpsLabel==null||frameBenchmark())return;
        hosting().poll();
        String networkInvite=getIntent().getStringExtra("networkInvite");
        if(networkInvite!=null&&nativeCombatReady()){
            getIntent().removeExtra("networkInvite");
            if(networkInvite.matches("[A-Za-z0-9_.-]+"))joinInvitation(new File(getFilesDir(),"multiplayer/"+networkInvite));
        }
        int result=nativeSessionResult();
        String[] messages={"","Game saved","Game loaded","No save exists in this slot","Saving is unavailable in this scene","Campaign started"};
        if(result>0&&result<messages.length){
            busySession=false;lastResultTime=android.os.SystemClock.elapsedRealtime();
            menu.setNotice(messages[result]);hud.notify(messages[result]);
        }
        // Arena action has no save/load completion result.
        if(busySession&&nativeCombatReady()){busySession=false;menu.setNotice("");}
        boolean network=multiplayer();
        if(wasMultiplayer&&!network){
            wasMultiplayer=false;lastResultTime=0;respawning=false;busySession=false;updateTouchVisibility();releaseKeys();menuOpen=true;nativeMenuSetting(0,1);
            controls.setVisibility(View.GONE);hud.setVisibility(View.GONE);menuLayer.setVisibility(View.VISIBLE);
            nativeMenuSetting(2,options.frameTarget());nativeMenuSetting(3,options.get("pacing"));
            menu.hasSession=false;menu.open(true);menu.setNotice(multiplayerStatus());menu.showMultiplayer();networkWasWaiting=false;
        }
        if(network)wasMultiplayer=true;
        boolean waiting=network&&nativeMultiplayerWaiting();
        if(waiting&&!networkWasWaiting){releaseKeys();openMenu();menu.showMultiplayer();}
        else if(network&&networkWasWaiting&&!waiting){releaseKeys();menu.setNotice("Connection restored. Resume when ready.");}
        networkWasWaiting=waiting;
        if(respawning&&network&&!dead()&&ready()){respawning=false;menu.setNotice("");resumeNetwork();}
        boolean dead=dead();
        if(network&&dead!=networkWasDead&&menuOpen&&menu.screen.equals("multiplayer"))menu.showMultiplayer();
        networkWasDead=dead;
        if(!dead)deathObservedAt=0;
        else if(!busySession) {
            long now=android.os.SystemClock.elapsedRealtime();
            if(deathObservedAt==0)deathObservedAt=now;
            // Let the death animation begin, then offer recovery without a
            // keyboard command or the legacy UWindow menu.
            if(!menuOpen&&now-deathObservedAt>=1500)openMenu();
            else if(menuOpen&&menu.screen.equals("pause"))menu.showHome();
        }
        syncFeedback();
        if(!initializedAudio&&nativeGetVisualMode()>=0){
            if(!getPreferences(MODE_PRIVATE).contains("menu.music"))options.items.get("music").value=nativeMenuVolume(0);
            if(!getPreferences(MODE_PRIVATE).contains("menu.effects"))options.items.get("effects").value=nativeMenuVolume(1);
            initializedAudio=true;
        }
        float fps=nativeGetEngineFps();
        hudFps=((fps>0?String.format(Locale.US,"FPS %.0f",fps):"FPS —")+" · "+scalingSummary(false)+" · "+options.items.get("aa").label());
        if(menuOpen){if(lastResultTime>0&&android.os.SystemClock.elapsedRealtime()-lastResultTime>4000){menu.setNotice("");lastResultTime=0;}menu.refresh();}
        fpsLabel.postDelayed(this,500);
    }};
    private final Runnable updateHud=new Runnable(){public void run(){
        if(!controlsVisible||hud==null||frameBenchmark())return;
        nativeMenuSetting(17,menuOpen&&((menuLayer!=null&&menuLayer.getVisibility()==View.VISIBLE)||(hud.editing&&hud.previewBackdrop))&&getIntent().getBooleanExtra("menuIdle",true)?1:0);
        if(!menuOpen||hud.editing)hud.update(hudValues,nativeHudSnapshot(hudValues),hudFps,options.get("counter")!=0);
        if(controls!=null){
            int ready=menuOpen||busySession?0:nativeBloodlustMoves();controls.setBloodlustMoves(ready);
            if(ready==3&&!bloodlustTouchScheduled&&getIntent().hasExtra("saveTest")&&getIntent().hasExtra("bloodlustGesture")){
                bloodlustTouchScheduled=true;String gesture=getIntent().getStringExtra("bloodlustGesture");
                controls.postDelayed(()->{if(!menuOpen&&!busySession)RuneTouchProbe.bloodlust(controls,gesture);},5000);
            }
            if(!slamTouchScheduled && !menuOpen && !busySession && nativeCombatReady() && getIntent().hasExtra("saveTest") && getIntent().getBooleanExtra("slamTouchProbe",false)) {
                slamTouchScheduled=true;controls.postDelayed(()->{if(!menuOpen&&!busySession)RuneTouchProbe.slam(controls);},3500);
            }
        }
        hud.postDelayed(this,33);
    }};
    private void addTouchControls(){
        controls=new RuneTouchView(this,getPreferences(MODE_PRIVATE),this);
        fpsLabel=new TextView(this);fpsLabel.setTextColor(RuneMenuView.IVORY);fpsLabel.setTextSize(12);fpsLabel.setBackgroundColor(0x99080c0d);fpsLabel.setPadding(dp(10),dp(5),dp(10),dp(5));
        FrameLayout.LayoutParams fpsPos=new FrameLayout.LayoutParams(-2,-2,Gravity.TOP|Gravity.RIGHT);fpsPos.topMargin=dp(12);fpsPos.rightMargin=dp(12);controls.addView(fpsLabel,fpsPos);
        mLayout.addView(controls,new RelativeLayout.LayoutParams(-1,-1));
        hud=new RuneHudView(this,getPreferences(MODE_PRIVATE),this);mLayout.addView(hud,new RelativeLayout.LayoutParams(-1,-1));
        menuLayer=new FrameLayout(this);menu=new RuneMenuView(this,this);menuLayer.addView(menu,new FrameLayout.LayoutParams(-1,-1));mLayout.addView(menuLayer,new RelativeLayout.LayoutParams(-1,-1));
        options.listener=this::applySettings;applySettings("initial");
        if(getIntent().getBooleanExtra("touchProbe",false)&&getIntent().hasExtra("saveTest"))controls.post(()->RuneTouchProbe.run(this,getPreferences(MODE_PRIVATE)));
        // Explicit map/probe launches remain automation-friendly. Ordinary launch
        // starts at the new title without silently loading/overwriting a save.
        if(getIntent().hasExtra("map")||getIntent().hasExtra("resumeSlot")||getIntent().getIntExtra("frames",0)>0)resume();
        else {menuOpen=true;nativeMenuSetting(0,1);controls.setVisibility(View.GONE);hud.setVisibility(View.GONE);menu.hasSession=false;menu.open(true);}
    }
    @Override public void onBackPressed(){if(controls.editing){controls.finish(false);return;}if(hud.editing){hud.finish(false);return;}if(menuOpen)menu.back();else openMenu();}
    @Override public boolean dispatchKeyEvent(KeyEvent event){
        if(controller!=null)controller.observe(event);
        int k=event.getKeyCode();boolean down=event.getAction()==KeyEvent.ACTION_DOWN;
        if(controls!=null&&controls.editing&&k!=KeyEvent.KEYCODE_VOLUME_UP&&k!=KeyEvent.KEYCODE_VOLUME_DOWN&&k!=KeyEvent.KEYCODE_VOLUME_MUTE)return controls.key(event);
        if(hud!=null&&hud.editing&&k!=KeyEvent.KEYCODE_VOLUME_UP&&k!=KeyEvent.KEYCODE_VOLUME_DOWN&&k!=KeyEvent.KEYCODE_VOLUME_MUTE)return hud.key(event);
        if(k==KeyEvent.KEYCODE_ESCAPE||k==KeyEvent.KEYCODE_BACK||k==KeyEvent.KEYCODE_BUTTON_START){
            if(down&&event.getRepeatCount()==0){if(menuOpen)menu.back();else openMenu();}return true;
        }
        if(k==KeyEvent.KEYCODE_VOLUME_UP||k==KeyEvent.KEYCODE_VOLUME_DOWN||k==KeyEvent.KEYCODE_VOLUME_MUTE)return super.dispatchKeyEvent(event);
        if(menuOpen){
            View editor=getCurrentFocus();
            if(editor instanceof android.widget.EditText&&k!=KeyEvent.KEYCODE_BUTTON_A&&k!=KeyEvent.KEYCODE_BUTTON_B&&k!=KeyEvent.KEYCODE_DPAD_UP&&k!=KeyEvent.KEYCODE_DPAD_DOWN&&k!=KeyEvent.KEYCODE_TAB){editor.dispatchKeyEvent(event);return true;}
            // Consume gameplay keys; D-pad, enter and gamepad A navigate widgets.
            if(k==KeyEvent.KEYCODE_BUTTON_A)return menu.controllerKey(event);
            if(k==KeyEvent.KEYCODE_BUTTON_B){if(down&&event.getRepeatCount()==0)menu.back();return true;}
            if(k==KeyEvent.KEYCODE_DPAD_UP||k==KeyEvent.KEYCODE_DPAD_DOWN||k==KeyEvent.KEYCODE_DPAD_LEFT||k==KeyEvent.KEYCODE_DPAD_RIGHT||k==KeyEvent.KEYCODE_DPAD_CENTER||k==KeyEvent.KEYCODE_ENTER||k==KeyEvent.KEYCODE_TAB||k==KeyEvent.KEYCODE_BUTTON_A)
                { menu.controllerKey(event); return true; }
            return true;
        }
        if(controller!=null&&controller.key(event))return true;
        return super.dispatchKeyEvent(event);
    }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent e){
        if(controls!=null&&controls.editing&&(e.getSource()&android.view.InputDevice.SOURCE_JOYSTICK)==android.view.InputDevice.SOURCE_JOYSTICK)return controls.motion(e);
        if(hud!=null&&hud.editing&&(e.getSource()&android.view.InputDevice.SOURCE_JOYSTICK)==android.view.InputDevice.SOURCE_JOYSTICK)return hud.motion(e);
        if(controller!=null&&controller.motion(e))return true;
        if(menuOpen)return true;
        return super.dispatchGenericMotionEvent(e);
    }
    private android.view.Display gameDisplay(){return mSurface!=null&&mSurface.getDisplay()!=null?mSurface.getDisplay():getWindowManager().getDefaultDisplay();}
    private android.view.Display.Mode refreshMode(int rate) {
        android.view.Display display=gameDisplay();if(display==null)return null;
        android.view.Display.Mode current=display.getMode();
        for(android.view.Display.Mode mode:display.getSupportedModes())
            if(mode.getPhysicalWidth()==current.getPhysicalWidth()&&mode.getPhysicalHeight()==current.getPhysicalHeight()&&Math.abs(mode.getRefreshRate()-rate)<.5f)return mode;
        return null;
    }
    public boolean refreshSupported(int rate){return rate==0||refreshMode(rate)!=null;}
    public String supportedRefreshRates(){
        android.view.Display display=gameDisplay();if(display==null)return "Display information unavailable";
        java.util.TreeSet<Integer> rates=new java.util.TreeSet<>();android.view.Display.Mode current=display.getMode();
        for(android.view.Display.Mode mode:display.getSupportedModes())if(mode.getPhysicalWidth()==current.getPhysicalWidth()&&mode.getPhysicalHeight()==current.getPhysicalHeight())rates.add(Math.round(mode.getRefreshRate()));
        return "Supported display rates: "+rates.toString().replace("[","").replace("]","")+" Hz";
    }
    private int refreshRequest(){return getIntent().hasExtra("refresh")?Math.max(0,Math.min(1000,getIntent().getIntExtra("refresh",120))):options.refreshTarget();}
    private void applyRefreshRate(){
        if(options==null||mSurface==null)return;
        int requested=refreshRequest();android.view.Display.Mode mode=refreshMode(requested);
        float rate=mode==null?0:mode.getRefreshRate();
        WindowManager.LayoutParams params=getWindow().getAttributes();
        params.preferredDisplayModeId=mode==null?0:mode.getModeId();params.preferredRefreshRate=rate;getWindow().setAttributes(params);
        if(android.os.Build.VERSION.SDK_INT>=30&&mSurface.getHolder().getSurface().isValid())mSurface.getHolder().getSurface().setFrameRate(rate,Surface.FRAME_RATE_COMPATIBILITY_DEFAULT);
        if(android.os.Build.VERSION.SDK_INT>=35){mLayout.setRequestedFrameRate(rate);getWindow().getDecorView().setRequestedFrameRate(rate);}
        android.util.Log.i("OpenRune","DISPLAY_REFRESH requested="+requested+" mode="+params.preferredDisplayModeId+" hint="+rate);
    }
    public String timingStatus(){
        android.view.Display display=gameDisplay();int request=refreshRequest();
        String report=display==null?"unavailable":String.format(Locale.US,"%.1f Hz",display.getMode().getRefreshRate());
        return "Game target: "+options.frameTarget()+" FPS · Refresh: "+(request==0?"Automatic":request+" Hz")+"\nAndroid reports: "+report+(request!=0&&!refreshSupported(request)?"\nRequested rate unavailable; using Automatic":"");
    }
    private int renderPercent(){return new int[]{100,75,50,25}[options.get("resolution")];}
    private int gsrMode(){return options.get("scalingMethod")==1?options.get("resolution"):0;}
    private int requestedSurfaceWidth,requestedSurfaceHeight;
    private void resizeGameSurface(){
        if(mSurface!=null)resizeGameSurface(mSurface);
    }
    private void resizeGameSurface(org.libsdl.app.SDLSurface surface){
        int percent=options.get("scalingMethod")==0?renderPercent():100;
        int width=Math.max(1,Math.round(surface.getWidth()*percent/100f));
        int height=Math.max(1,Math.round(surface.getHeight()*percent/100f));
        if(surface.getWidth()==0||surface.getHeight()==0)return;
        if(width==requestedSurfaceWidth&&height==requestedSurfaceHeight)return;
        requestedSurfaceWidth=width;requestedSurfaceHeight=height;
        if(percent==100)surface.getHolder().setSizeFromLayout();
        else surface.getHolder().setFixedSize(width,height);
        android.util.Log.i("OpenRune","SYSTEM_SCALE_REQUEST buffer="+width+"x"+height+" view="+surface.getWidth()+"x"+surface.getHeight());
    }
    public String scalingStatus(){return scalingSummary(true);}
    private String scalingSummary(boolean full){
        long snapshot=nativeScalingSnapshot();
        if(snapshot==0||mSurface==null)return "Preparing renderer…";
        int rw=(int)(snapshot&32767),rh=(int)((snapshot>>>15)&32767);
        int ow=(int)((snapshot>>>30)&32767),oh=(int)((snapshot>>>45)&32767);
        int dw=mSurface.getWidth(),dh=mSurface.getHeight();
        boolean gsr=(snapshot&(1L<<60))!=0;
        String method=gsr?"GSR":ow!=dw||oh!=dh?"Hardware Scaling":"Native";
        String active=method+" · "+rw+"×"+rh;
        if(!full)return (gsr?"Software":method.equals("Hardware Scaling")?"Hardware":"Native")+" "+(dw>0?Math.round(100f*rw/dw):renderPercent())+"%";
        int expectedW=Math.max(1,Math.round(dw*renderPercent()/100f));
        int expectedH=Math.max(1,Math.round(dh*renderPercent()/100f));
        boolean pending=rw!=expectedW||rh!=expectedH||gsr!=(gsrMode()!=0)||
            ow!=(options.get("scalingMethod")==0?expectedW:dw)||oh!=(options.get("scalingMethod")==0?expectedH:dh);
        return (pending?"Applying selection…\n":"")+active+" → "+dw+"×"+dh+" display";
    }
    @Override protected org.libsdl.app.SDLSurface createSDLSurface(android.content.Context context) {
        org.libsdl.app.SDLSurface surface=new org.libsdl.app.SDLSurface(context) {
            @Override public boolean onTouch(View v,MotionEvent event) {
                if(getWidth()==0||getHeight()==0||(mWidth==getWidth()&&mHeight==getHeight()))return super.onTouch(v,event);
                MotionEvent scaled=MotionEvent.obtain(event);
                android.graphics.Matrix matrix=new android.graphics.Matrix();
                matrix.setScale(mWidth/getWidth(),mHeight/getHeight());
                scaled.transform(matrix);
                try{return super.onTouch(v,scaled);}finally{scaled.recycle();}
            }
        };
        // SDL adds the surface without explicit sizing. SurfaceView otherwise
        // measures from its fixed buffer, feeding reduced dimensions back into
        // the layout on a live switch. Keep layout and buffer sizes independent.
        surface.setLayoutParams(new RelativeLayout.LayoutParams(-1,-1));
        surface.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->resizeGameSurface(surface));
        return surface;
    }
    @Override protected String[] getLibraries(){return new String[]{"SDL2","openal","SurrealVideo","runeclassic"};}
    @Override public void setOrientationBis(int width,int height,boolean resizable,String hint){super.setOrientationBis(width,height,resizable,getIntent().getBooleanExtra("reverseLandscape",false)?"LandscapeRight":"LandscapeLeft LandscapeRight");}
    @Override protected void onCreate(Bundle state){
        try(InputStream in=getAssets().open("SurrealEngine.pk3");FileOutputStream out=new FileOutputStream(new File(getFilesDir(),"SurrealEngine.pk3"))){byte[] b=new byte[65536];int n;while((n=in.read(b))!=-1)out.write(b,0,n);}catch(Exception e){throw new RuntimeException("Could not prepare game resources",e);}
        options=new RuneSettings(getPreferences(MODE_PRIVATE),initialClarity(),initialWorldFeatures(),initialEnhanced(),initialUpscale());
        options.items.get("texturePack").value=initialTexturePack();
        if(getIntent().hasExtra("fireEffects"))options.items.get("fireEffects").value=getIntent().getBooleanExtra("fireEffects",true)?1:0;
        // Diagnostic overrides are session-only and never migrate into preferences.
        if(getIntent().hasExtra("upscale")) {
            options.items.get("resolution").value=getIntent().getBooleanExtra("upscale",false)?1:0;
            options.items.get("scalingMethod").value=1;
        }
        if(getIntent().hasExtra("surfacePercent")) {
            int percent=getIntent().getIntExtra("surfacePercent",100);
            if((percent!=25&&percent!=50&&percent!=75&&percent!=100)||!getIntent().hasExtra("saveTest"))throw new IllegalArgumentException("Invalid isolated surface diagnostic");
            options.items.get("scalingMethod").value=0;
            options.items.get("resolution").value=percent==25?3:percent==50?2:percent==75?1:0;
        }
        if(getIntent().hasExtra("msaa"))options.items.get("aa").value=Math.max(0,Math.min(2,getIntent().getIntExtra("msaa",2)));
        if(getIntent().hasExtra("pacing"))options.items.get("pacing").value=getIntent().getIntExtra("pacing",1)!=0?1:0;
        super.onCreate(state);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if(getIntent().hasExtra("aspectPreview")) {
            // App-only layout diagnostic: never change Android display settings.
            float aspect=getIntent().getFloatExtra("aspectPreview",0);
            if(!getIntent().hasExtra("saveTest")||!Float.isFinite(aspect)||aspect<1||aspect>4)throw new IllegalArgumentException("Invalid isolated aspect preview");
            float inches=getIntent().getFloatExtra("previewInches",0);
            if(!Float.isFinite(inches)||inches<0||inches>20)throw new IllegalArgumentException("Invalid preview size");
            FrameLayout content=findViewById(android.R.id.content);
            content.setBackgroundColor(0xff000000);
            Runnable fit=()->{
                int w=content.getWidth(),h=content.getHeight();if(w<=0||h<=0)return;
                if((float)w/h>aspect)w=Math.round(h*aspect);else h=Math.round(w/aspect);
                if(inches>0) {
                    android.util.DisplayMetrics metrics=getResources().getDisplayMetrics();
                    double physicalHeight=inches/Math.sqrt(aspect*aspect+1);
                    int targetW=(int)Math.round(physicalHeight*aspect*metrics.xdpi);
                    int targetH=(int)Math.round(physicalHeight*metrics.ydpi);
                    float fitScale=Math.min(1f,Math.min((float)w/Math.max(1,targetW),(float)h/Math.max(1,targetH)));
                    w=Math.max(1,Math.round(targetW*fitScale));h=Math.max(1,Math.round(targetH*fitScale));
                }
                FrameLayout.LayoutParams size=new FrameLayout.LayoutParams(w,h,android.view.Gravity.CENTER);
                mLayout.setLayoutParams(size);
            };
            content.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->fit.run());content.post(fit);
        }
        if(getIntent().getBooleanExtra("reverseLandscape",false))setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE);
        mSurface.getHolder().addCallback(new SurfaceHolder.Callback(){
            public void surfaceCreated(SurfaceHolder h){applyRefreshRate();}
            public void surfaceChanged(SurfaceHolder h,int f,int w,int height){applyRefreshRate();}
            public void surfaceDestroyed(SurfaceHolder h){}
        });
        applyRefreshRate();
        controller=new RuneController(this,this,getIntent().getBooleanExtra("controllerProbe",false));
        rumble=new RuneRumble(this,()->controller.feedbackDevice(),MainActivity::nativeFeedbackPoll,getIntent().getBooleanExtra("rumbleProbe",false));
        addTouchControls();
    }
    @Override protected void onPause(){
        mLayout.removeCallbacks(textureInstallCheck);
        controlsVisible=false;if(rumble!=null)rumble.foreground(false);if(hud!=null){hud.removeCallbacks(updateHud);hud.suspend();}if(fpsLabel!=null)fpsLabel.removeCallbacks(updateStatus);releaseKeys();nativeMenuSetting(0,1);super.onPause();
    }
    @Override protected void onResume(){super.onResume();mLayout.removeCallbacks(textureInstallCheck);if(pendingTextureQuality>0)mLayout.postDelayed(textureInstallCheck, 1000);applyRefreshRate();controlsVisible=true;nativeMenuSetting(0,menuOpen?1:0);if(fpsLabel!=null){fpsLabel.removeCallbacks(updateStatus);fpsLabel.post(updateStatus);}if(hud!=null){hud.removeCallbacks(updateHud);hud.post(updateHud);}if(rumble!=null)rumble.foreground(hasWindowFocus());syncFeedback();}
    @Override public void onWindowFocusChanged(boolean focus){if(!focus)releaseKeys();super.onWindowFocusChanged(focus);if(rumble!=null)rumble.foreground(focus&&controlsVisible);syncFeedback();}
    @Override protected void onDestroy(){if(discovery!=null)discovery.close();if(hosting!=null)hosting.stop();if(rumble!=null)rumble.close();if(controller!=null)controller.close();super.onDestroy();android.os.Process.killProcess(android.os.Process.myPid());}
    @Override protected String[] getArguments() {
        ArrayList<String> args=new ArrayList<>();
        args.add("--direct");
        if(!getIntent().hasExtra("waterModel"))args.add("--water-model="+(options!=null&&options.get("water")==3?3:1));
        args.add("--rune-physics="+getIntent().getIntExtra("physicsMode",options==null?RuneSettings.PHYSICS_DEFAULT:options.get("physics")));
        if(getIntent().hasExtra("saveTest") && (getIntent().getIntExtra("frames",0)>0 || (getIntent().getIntExtra("networkSeconds",0)>0 && getIntent().getIntExtra("networkSeconds",0)<=300))) {
            if(getIntent().getBooleanExtra("physicsTrace",false))args.add("--rune-puppet-trace");
            if(getIntent().hasExtra("breathDemo"))args.add("--breath-demo="+getIntent().getStringExtra("breathDemo"));
            if(getIntent().hasExtra("breathEffects"))args.add("--rune-breath="+(getIntent().getBooleanExtra("breathEffects",true)?1:0));
            if(getIntent().getBooleanExtra("breathTrace",false))args.add("--trace-rune-breath");
            if(getIntent().getBooleanExtra("physicsMuscles",false))args.add("--physics-muscles");
            if(getIntent().hasExtra("puppetMode"))args.add("--rune-puppet="+(getIntent().getBooleanExtra("puppetMode",true)?1:0));
            if(getIntent().hasExtra("footIK"))args.add("--rune-ik="+(getIntent().getBooleanExtra("footIK",true)?1:0));
            if(getIntent().getBooleanExtra("nativePuppetProbe",false) || getIntent().getBooleanExtra("nativeIKProbe",false)) {
                args.add("--rune-probe");args.add("--rune-physics-probe");
                args.add(getIntent().getBooleanExtra("nativePuppetProbe",false)?"--rune-puppet-probe":"--rune-ik-probe");
            }
        }

        if(getIntent().hasExtra("physicsImpacts")&&getIntent().hasExtra("saveTest")&&getIntent().getIntExtra("frames",0)>0){int value=getIntent().getIntExtra("physicsImpacts",1);if(value!=0&&value!=1)throw new IllegalArgumentException("Invalid isolated impact comparison");args.add("--physics-impacts="+value);}
        if(getIntent().getBooleanExtra("physicsEnvironment",false)&&getIntent().hasExtra("saveTest")&&getIntent().getIntExtra("frames",0)>0)args.add("--physics-environment");
        if(getIntent().getBooleanExtra("physicsTouch",false)&&getIntent().hasExtra("saveTest")&&getIntent().getIntExtra("frames",0)>0)args.add("--physics-touch");
        if(getIntent().getBooleanExtra("physicsRepeat",false)&&getIntent().hasExtra("saveTest")&&getIntent().getIntExtra("frames",0)>0)args.add("--physics-repeat");
        if(getIntent().getBooleanExtra("physicsPlayground",false)&&getIntent().hasExtra("saveTest")){args.add("--physics-playground");args.add("--physics-captures="+new File(getFilesDir(),"physics-stage").getAbsolutePath());}
        args.add("--sound-remaster="+getIntent().getIntExtra("soundRemaster",options==null?0:options.get("soundRemaster")));
        args.add("--snow="+(options!=null?(options.comparing?-2:options.get("snow")-1):-1));
        args.add("--rune-effects="+(options!=null?options.get("fireEffects"):1));
        if(getIntent().getBooleanExtra("effectsProbe",false)&&getIntent().hasExtra("saveTest"))args.add("--probe-effects");
        String driver=getIntent().getStringExtra("vulkanDriver");
        if(driver==null)driver=new File(getFilesDir(),"turnip-enabled").exists()?"turnip":"system";
        if(!driver.equals("turnip")&&!driver.equals("system"))throw new IllegalArgumentException("Unknown Vulkan driver");
        args.add("--android-vulkan-driver="+driver);
        args.add("--android-native-lib-dir="+getApplicationInfo().nativeLibraryDir+"/");
        args.add("--rune-animation-interpolation="+getIntent().getIntExtra("animationSmooth",1));
        if(!getIntent().hasExtra("map")&&!getIntent().hasExtra("resumeSlot")&&getIntent().getIntExtra("frames",0)==0)args.add("--menu-title");
        args.add("--close-menu");
        if (frameBenchmark()) {
            args.add("--frame-benchmark");
            if (getIntent().hasExtra("probeWorkers"))
                args.add("--environment-probe-workers=" + (getIntent().getBooleanExtra("probeWorkers", false) ? 1 : 0));

            if (getIntent().hasExtra("pipelineCache"))
                args.add("--render-pipeline-cache=" + (getIntent().getBooleanExtra("pipelineCache", false) ? 1 : 0));
            if (getIntent().hasExtra("sceneStores"))
                args.add("--render-scene-stores=" + getIntent().getIntExtra("sceneStores", 0));
            if (getIntent().hasExtra("reliefMissCache"))
                args.add("--cache-relief-misses=" + (getIntent().getBooleanExtra("reliefMissCache", false) ? 1 : 0));
        }
        String launchMap=getIntent().getStringExtra("map");
        args.add("--url="+(launchMap==null?"DM-Hildir.run":launchMap));
        args.add("--maxfps="+getIntent().getIntExtra("fps",0));
        args.add("--visual-enhanced="+(initialEnhanced()?1:0));
        args.add("--clarity="+initialClarity());
        args.add("--world-features="+initialWorldFeatures());
        args.add("--shadow-lights="+initialShadowBudget());
        args.add("--texture-pack="+initialTexturePack());
        args.add("--upscale="+gsrMode());
        args.add("--frame-pacing="+1);
        if(getIntent().hasExtra("seed")) args.add("--probe-seed="+getIntent().getIntExtra("seed",1));
        if(getIntent().hasExtra("cameraFade")) args.add("--rune-camera-fade="+getIntent().getIntExtra("cameraFade",1));
        if(getIntent().hasExtra("cameraCoverage")) args.add("--rune-camera-coverage="+getIntent().getIntExtra("cameraCoverage",1));
        if(getIntent().getBooleanExtra("cameraProbe",false)) args.add("--probe-camera");
        String saveTest=getIntent().getStringExtra("saveTest");
        if(saveTest!=null) {
            if(!saveTest.matches("[A-Za-z0-9_-]+")) throw new IllegalArgumentException("Invalid save test name");
            args.add("--save-directory="+new File(getFilesDir(),"validation-saves/"+saveTest).getAbsolutePath());
        }
        if(getIntent().getBooleanExtra("saveFlow",false)) args.add("--probe-save-flow");
        for(String key:new String[]{"travelExit","travelURL"}) if(getIntent().hasExtra(key))
            args.add((key.equals("travelExit")?"--probe-travel-exit=":"--probe-travel-url=")+getIntent().getStringExtra(key));
        if(getIntent().hasExtra("bashProbe")) args.add("--probe-bash="+getIntent().getStringExtra("bashProbe"));
        if(getIntent().getBooleanExtra("feedbackFixture",false))args.add("--probe-feedback");
        if(getIntent().hasExtra("kickProbe")) args.add("--probe-kick="+getIntent().getStringExtra("kickProbe"));
        if(getIntent().getBooleanExtra("gibProbe",false)) args.add("--probe-gibs");
        if(getIntent().getBooleanExtra("ropeProbe",false)) args.add("--probe-rope");
        if(getIntent().hasExtra("resumeSlot")) args.add("--probe-resume-slot="+getIntent().getIntExtra("resumeSlot",-1));
        if(getIntent().hasExtra("mailbox")) args.add("--render-mailbox="+getIntent().getIntExtra("mailbox",1));
        int frames=getIntent().getIntExtra("frames",0);
        if(getIntent().getBooleanExtra("goreMaterialProbe",false)) {
            if(saveTest==null || frames<540 || frames>1000)
                throw new IllegalArgumentException("Material comparison requires bounded isolated saves");
            args.add("--probe-gore");args.add("--probe-enemy=GoblinWarrior");
            args.add("--capture-gore-material-comparison");args.add("--capture-present-frame");
        }
        if(frames>0) {
            args.add("--maxframes="+frames);
            if (!frameBenchmark() || getIntent().getBooleanExtra("benchmarkCapture", false))
                args.add("--capture-frame="+new File(getFilesDir(),"frame.ppm").getAbsolutePath());
        }
        if(getIntent().hasExtra("saveTest")) {
            String nativeFixture=getIntent().getStringExtra("networkNativeFixture");
            if("dual".equals(nativeFixture)||"blood".equals(nativeFixture)||"bash".equals(nativeFixture)) {
                args.add("--rune-net-native-fixture="+nativeFixture);
                File captures=new File(getFilesDir(),"native-combat-captures");captures.mkdirs();
                args.add("--rune-net-native-captures="+captures.getAbsolutePath());
            }
            if(getIntent().getBooleanExtra("networkTestInput",false)) args.add("--rune-net-test-input");
            if(getIntent().getBooleanExtra("networkSparringDefender",false)) args.add("--rune-net-sparring-defender");
            int networkSeconds=getIntent().getIntExtra("networkSeconds",0);
            if(networkSeconds>0&&networkSeconds<=300) args.add("--rune-net-seconds="+networkSeconds);
            if(getIntent().hasExtra("networkPrediction")) args.add("--rune-net-prediction="+getIntent().getIntExtra("networkPrediction",1));
        }
        if(getIntent().hasExtra("probeTickHz")) args.add("--probe-tick-hz="+getIntent().getIntExtra("probeTickHz",0));
        if(getIntent().getBooleanExtra("localStairProbe",false)) {
            if(saveTest==null || frames<1100 || frames>1500)
                throw new IllegalArgumentException("Local stair check requires bounded isolated saves");
            args.add("--probe-local-stairs="+new File(getFilesDir(),"local-stairs.csv").getAbsolutePath());
            args.add("--probe-local-stair-captures="+new File(getFilesDir(),"local-stairs").getAbsolutePath());
            args.add("--rune-local-stair-smoothing="+(getIntent().getBooleanExtra("localStairSmoothing",true)?1:0));
        }
        if(getIntent().getBooleanExtra("slamGestureProbe",false) || getIntent().getBooleanExtra("slamTouchProbe",false)) {
            boolean touch=getIntent().getBooleanExtra("slamTouchProbe",false);
            if(saveTest==null || frames<(touch?900:500) || frames>1500)throw new IllegalArgumentException("Slam check requires bounded isolated saves");
            args.add("--probe-slam-gesture");
            args.add("--probe-slam-captures="+new File(getFilesDir(),"slam-stage").getAbsolutePath());
            if(touch)args.add("--probe-slam-touch");
            if(getIntent().getBooleanExtra("slamSideView",false))args.add("--probe-slam-side-view");
        }
        int profile=getIntent().getIntExtra("profile",0);
        if(profile>0) args.add("--profile-frames="+profile);
        if(profile>0 && frames>0 && saveTest!=null && getIntent().getBooleanExtra("profilePasses",false))
            args.add("--profile-passes=1");
        if(getIntent().getBooleanExtra("lightingCompare",false)) args.add("--capture-lighting-comparison");
        if(getIntent().getBooleanExtra("textureProbe",false)) args.add("--probe-texture-pack");
        if(getIntent().getBooleanExtra("textureCompare",false)) args.add("--capture-texture-comparison");
        if(saveTest!=null && frames>0) {
            if(getIntent().getBooleanExtra("capturePresent",false))args.add("--capture-present-frame");
            if(getIntent().hasExtra("waterModel")){int value=getIntent().getIntExtra("waterModel",1);if(value<0||value>3)throw new IllegalArgumentException("Invalid isolated water model");args.add("--water-model="+value);}
            if(getIntent().hasExtra("waterUnderwater"))args.add("--water-underwater="+(getIntent().getBooleanExtra("waterUnderwater",true)?1:0));
            if(getIntent().getBooleanExtra("waterProbe",false))args.add("--probe-modern-water-only");
            if(getIntent().hasExtra("crestSpecialized"))args.add("--crest-specialized="+(getIntent().getBooleanExtra("crestSpecialized",true)?1:0));
            if(getIntent().hasExtra("crestInpassCapture"))args.add("--crest-inpass-capture="+(getIntent().getBooleanExtra("crestInpassCapture",true)?1:0));
            if(getIntent().getBooleanExtra("crestDebug",false))args.add("--crest-debug-water");
            if(getIntent().getBooleanExtra("crestDump",false))args.add("--crest-dump="+new java.io.File(getFilesDir(),"crest-float.bin").getAbsolutePath());
            if(getIntent().getBooleanExtra("traceWater",false))args.add("--trace-rune-water");
        }
        if(getIntent().getBooleanExtra("vulkanProbe",false)) args.add("--probe-vulkan");
        if(getIntent().getBooleanExtra("shadowBudgetProbe",false)) args.add("--probe-shadow-budget");
        if(getIntent().getBooleanExtra("traceShadows",false)) args.add("--trace-shadows");
        if(getIntent().getBooleanExtra("traceShadowTransitions",false)&&saveTest!=null)args.add("--trace-shadow-transitions");
        if(getIntent().hasExtra("opaqueEarlyDepth")) args.add("--render-opaque-early-depth="+getIntent().getIntExtra("opaqueEarlyDepth",1));
        if(getIntent().hasExtra("fullPrecisionLighting")) args.add("--render-full-precision-lighting="+getIntent().getIntExtra("fullPrecisionLighting",0));
        if(getIntent().hasExtra("shadowReceivers")) args.add("--render-shadow-receivers="+getIntent().getIntExtra("shadowReceivers",1));
        if(getIntent().hasExtra("cullLighting")) args.add("--render-cull-lighting="+getIntent().getIntExtra("cullLighting",1));
        if(getIntent().hasExtra("groupShadows")) args.add("--render-group-shadows="+getIntent().getIntExtra("groupShadows",0));
        if(getIntent().hasExtra("packedHdr")) args.add("--render-packed-hdr="+getIntent().getIntExtra("packedHdr",0));
        if(getIntent().hasExtra("uniformShadowData")) args.add("--render-uniform-shadow-data="+getIntent().getIntExtra("uniformShadowData",0));
        if(getIntent().hasExtra("moverVisibilityCache")&&getIntent().hasExtra("saveTest")) args.add("--render-mover-visibility-cache="+getIntent().getIntExtra("moverVisibilityCache",1));
        if(getIntent().hasExtra("fastWorldVisibility")&&getIntent().hasExtra("saveTest")) args.add("--render-fast-world-visibility="+getIntent().getIntExtra("fastWorldVisibility",1));
        if(getIntent().hasExtra("liftProbe")&&getIntent().hasExtra("saveTest")) args.add("--probe-lift="+getIntent().getStringExtra("liftProbe"));
        if(getIntent().hasExtra("omitHit")) args.add("--render-omit-hit="+getIntent().getIntExtra("omitHit",0));
        if(getIntent().hasExtra("inpassResolve")) args.add("--render-inpass-resolve="+getIntent().getIntExtra("inpassResolve",0));
        if(saveTest!=null && frames>0 && getIntent().hasExtra("specializedLighting"))
            args.add("--render-specialized-lighting="+getIntent().getIntExtra("specializedLighting",0));
        if(getIntent().hasExtra("discardHit")) args.add("--render-discard-hit="+getIntent().getIntExtra("discardHit",1));
        if(getIntent().getBooleanExtra("validation",false)) args.add("--vulkan-validation");
        if(getIntent().hasExtra("overlap")) args.add("--render-overlap="+getIntent().getIntExtra("overlap",0));
        if(getIntent().hasExtra("scale")) args.add("--render-scale="+getIntent().getFloatExtra("scale",1.0f));
        if(getIntent().hasExtra("images")) args.add("--swapchain-images="+getIntent().getIntExtra("images",3));
        args.add("--render-msaa="+getIntent().getIntExtra("msaa",getPreferences(MODE_PRIVATE).getInt("menu.aa",2)));
        if(getIntent().hasExtra("vsync")) args.add("--render-vsync="+getIntent().getIntExtra("vsync",1));
        int forward=getIntent().getIntExtra("forward",0);
        if(forward>0) args.add("--probe-forward-frames="+forward);
        int fire=getIntent().getIntExtra("fire",0);
        if(fire>0) args.add("--probe-fire-frame="+fire);
        String target=getIntent().getStringExtra("target");
        if(target!=null) args.add("--probe-target="+target);
        String enemy=getIntent().getStringExtra("enemy");
        String weaponProbe=getIntent().getStringExtra("weaponProbe");
        if(weaponProbe!=null) args.add("--probe-weapon="+weaponProbe);
        if(getIntent().hasExtra("weaponClass"))args.add("--probe-weapon-class="+getIntent().getStringExtra("weaponClass"));
        if(getIntent().getBooleanExtra("weaponTouch",false)) args.add("--probe-weapon-touch");
        if(getIntent().hasExtra("limbJoint")) args.add("--probe-limb-joint="+getIntent().getIntExtra("limbJoint",-1));
        if(enemy!=null) args.add("--probe-enemy="+enemy);
        if(getIntent().getBooleanExtra("corpseHandProbe",false)&&saveTest!=null&&frames>0){
            args.add("--probe-corpse");args.add("--corpse-hand-audit");
            args.add("--corpse-demo-capture="+new File(getFilesDir(),"corpse-hands").getAbsolutePath());
        }
        if(getIntent().hasExtra("probeStart")) args.add("--probe-start="+getIntent().getStringExtra("probeStart"));
        if(getIntent().hasExtra("probeView")) args.add("--probe-view="+getIntent().getStringExtra("probeView"));
        if(getIntent().getBooleanExtra("checkReach",false)) args.add("--probe-check-reach");
        String shield=getIntent().getStringExtra("shield");
        if(shield!=null) args.add("--probe-shield="+shield);
        if(getIntent().hasExtra("enemyFrame")) args.add("--probe-enemy-frame="+getIntent().getIntExtra("enemyFrame",30));
        if(getIntent().hasExtra("fireHold")) args.add("--probe-fire-hold="+getIntent().getIntExtra("fireHold",60));
        if(getIntent().hasExtra("fireRepeat")) args.add("--probe-fire-repeat="+getIntent().getIntExtra("fireRepeat",0));
        if(getIntent().hasExtra("block")) args.add("--probe-block-frame="+getIntent().getIntExtra("block",0));
        if(getIntent().hasExtra("blockHold")) args.add("--probe-block-hold="+getIntent().getIntExtra("blockHold",600));
        if(getIntent().getBooleanExtra("bloodlustProbe",false)&&getIntent().hasExtra("saveTest")){
            args.add("--probe-bloodlust");
            if(getIntent().getBooleanExtra("bloodlustInfinite",false))args.add("--probe-bloodlust-infinite");
        }
        if(getIntent().getBooleanExtra("dualProbe",false))args.add("--probe-dual");
        if(getIntent().getBooleanExtra("runePowerProbe",false)&&getIntent().hasExtra("saveTest"))args.add("--probe-rune-power");
        if(getIntent().getBooleanExtra("dualGuard",false))args.add("--probe-dual-guard");
        if(getIntent().getBooleanExtra("traceCombat",false)) args.add("--trace-rune-combat");
        if(getIntent().getBooleanExtra("enemyClearLane",false)) args.add("--probe-enemy-clear-lane");
        if(getIntent().getBooleanExtra("endurance",false)) args.add("--probe-endurance");
        args.add(new File(getFilesDir(),"game").getAbsolutePath());
        return args.toArray(new String[0]);
    }
}
