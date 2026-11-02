package org.runeclassic.port;

import android.content.Context;
import android.hardware.input.InputManager;
import android.media.AudioAttributes;
import android.os.Build;
import android.os.Handler;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.util.Log;
import android.view.InputDevice;
import java.util.function.IntSupplier;
import java.util.function.LongSupplier;

/** Capability-based Android backend. All methods run on the UI thread. */
final class RuneRumble implements InputManager.InputDeviceListener {
    private final Context context;
    private final InputManager inputs;
    private final IntSupplier controller;
    private final LongSupplier poll;
    private final Handler handler=new Handler();
    private final AudioAttributes audio=new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build();
    private final boolean trace;
    private Vibrator output;
    private int outputId=Integer.MIN_VALUE,lastController=Integer.MIN_VALUE,mode=0,intensity=65,priority;
    private boolean enabled=true,foreground,dirty=true;
    private long end,lastStart;
    private String status="Checking vibration support…";
    RuneRumble(Context c,IntSupplier controller,LongSupplier poll,boolean trace) {
        context=c;this.controller=controller;this.poll=poll;this.trace=trace;
        inputs=(InputManager)c.getSystemService(Context.INPUT_SERVICE);
        inputs.registerInputDeviceListener(this,handler);
    }
    private Vibrator controllerVibrator(InputDevice d) {
        if(d==null||!RuneController.gamepad(d))return null;
        Vibrator v=Build.VERSION.SDK_INT>=31?d.getVibratorManager().getDefaultVibrator():d.getVibrator();
        return v!=null&&v.hasVibrator()?v:null;
    }
    private void resolve() {
        int selected=controller.getAsInt();
        if(!dirty&&selected==lastController)return;
        stop();output=null;outputId=Integer.MIN_VALUE;dirty=false;lastController=selected;
        try {
            if(mode!=2) {
                InputDevice active=InputDevice.getDevice(selected);
                if(active!=null) {output=controllerVibrator(active);if(output!=null)outputId=active.getId();}
                else {
                    // Before input selects a player, only choose an unambiguous
                    // rumble-capable controller. Never choose by brand/name.
                    int count=0;
                    for(int id:InputDevice.getDeviceIds()) {
                        Vibrator v=controllerVibrator(InputDevice.getDevice(id));
                        if(v!=null){output=v;outputId=id;count++;}
                    }
                    if(count>1){output=null;outputId=Integer.MIN_VALUE;}
                }
            }
            if(output==null&&mode!=1) {
                Vibrator v=Build.VERSION.SDK_INT>=31?((VibratorManager)context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE)).getDefaultVibrator():(Vibrator)context.getSystemService(Context.VIBRATOR_SERVICE);
                if(v!=null&&v.hasVibrator()){output=v;outputId=-1;}
            }
            status=output==null?"No supported vibration output":
                (outputId==-1?"Device vibration":InputDevice.getDevice(outputId).getName())+
                (output.hasAmplitudeControl()?" · variable strength":" · pulse-length strength");
            if(trace)Log.i("RuneRumble","output="+outputId+" "+status);
        } catch(RuntimeException e){output=null;status="Vibration unavailable";Log.w("RuneRumble",status,e);}
    }
    void configure(boolean enabled,int intensity,int mode) {
        if(this.mode!=mode)dirty=true;
        this.enabled=enabled;this.intensity=Math.max(0,Math.min(100,intensity));this.mode=mode;
        stop();resolve();
    }
    String status(){resolve();return !enabled?"Rumble is off":intensity==0?"Rumble intensity is zero":status;}
    void foreground(boolean value) {
        foreground=value;handler.removeCallbacks(tick);stop();
        if(value){dirty=true;handler.post(tick);}
    }
    void stop() {
        if(output!=null)try{output.cancel();}catch(RuntimeException ignored){}
        end=0;priority=0;
    }
    void play(long packed,boolean preview) {
        if(packed==0||!foreground||!enabled||intensity==0)return;
        resolve();if(output==null)return;
        int duration=(int)(packed&1023),low=(int)((packed>>10)&255),high=(int)((packed>>18)&255),p=(int)((packed>>30)&7);
        long now=SystemClock.uptimeMillis();
        if(!preview&&now<end&&(p<=priority||now-lastStart<40))return;
        float strength=Math.max(low,high)/255f*intensity/100f;
        if(strength<=0)return;
        try {
            // Android's aggregate vibrator has no portable low/high motor map.
            // Preserve that intent upstream; use a bounded mono pulse here.
            int amplitude=output.hasAmplitudeControl()?Math.max(1,Math.round(255*strength)):VibrationEffect.DEFAULT_AMPLITUDE;
            if(amplitude==VibrationEffect.DEFAULT_AMPLITUDE)duration=Math.max(8,Math.round(duration*(.25f+.75f*strength)));
            // A longer preview makes capability testing easier to feel. Actual
            // game events retain their short, bounded impact durations.
            duration=preview?400:Math.max(8,Math.min(180,duration));
            output.vibrate(VibrationEffect.createOneShot(duration,amplitude),audio);
            lastStart=now;end=now+duration;priority=p;
            if(trace)Log.i("RuneRumble","play event="+((packed>>26)&15)+" output="+outputId+" ms="+duration+" amplitude="+amplitude+" preview="+preview);
        } catch(RuntimeException e){stop();status="Vibration request failed";Log.w("RuneRumble",status,e);}
    }
    private final Runnable tick=new Runnable(){public void run(){if(!foreground)return;resolve();play(poll.getAsLong(),false);handler.postDelayed(this,16);}};
    void close(){foreground(false);inputs.unregisterInputDeviceListener(this);}
    public void onInputDeviceAdded(int id){dirty=true;}
    public void onInputDeviceChanged(int id){dirty=true;if(id==outputId)stop();}
    public void onInputDeviceRemoved(int id){dirty=true;if(id==outputId){stop();output=null;}}
}
