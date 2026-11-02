package org.runeclassic.port;

import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import java.util.HashSet;

/** Android owns gamepad routing; consuming these events prevents duplicate SDL actions. */
final class RuneController implements InputManager.InputDeviceListener {
    interface Host {
        void controllerKey(int key, boolean down);
        void controllerAxes(float x,float y,float yaw,float pitch);
        boolean controllerMenu();
        void controllerNavigate(int key);
        float controllerSensitivity();
        boolean controllerInvert();
        void controllerConnectionChanged();
    }
    private final Host host;
    private final InputManager manager;
    private final Handler handler=new Handler();
    private final HashSet<Integer> buttons=new HashSet<>(), sent=new HashSet<>();
    private static final int NO_DEVICE=Integer.MIN_VALUE;
    private int active=NO_DEVICE,nav=0;
    private int lastDevice=NO_DEVICE;
    int feedbackDevice(){return lastDevice;}
    void observe(KeyEvent event){if(gamepad(event.getDevice())&&(active==NO_DEVICE||active==event.getDeviceId()))lastDevice=event.getDeviceId();}
    private float hatX,hatY;
    private boolean leftTrigger,rightTrigger;
    private long lastTrace;
    private final boolean trace;
    private boolean connected;
    boolean connected(){return connected;}
    private void refreshConnection(){
        boolean present=false;
        for(int id:InputDevice.getDeviceIds())if(gamepad(InputDevice.getDevice(id))){present=true;break;}
        if(present!=connected){connected=present;host.controllerConnectionChanged();}
    }
    RuneController(Context context,Host host,boolean trace){
        this.host=host;this.trace=trace;
        manager=(InputManager)context.getSystemService(Context.INPUT_SERVICE);
        manager.registerInputDeviceListener(this,handler);
        refreshConnection();
        Log.i("RuneController","Connected: "+status());
    }
    static boolean gamepad(InputDevice d){return d!=null&&((d.getSources()&InputDevice.SOURCE_GAMEPAD)==InputDevice.SOURCE_GAMEPAD||(d.getSources()&InputDevice.SOURCE_JOYSTICK)==InputDevice.SOURCE_JOYSTICK);}
    String status(){
        InputDevice selected=InputDevice.getDevice(active);
        if(gamepad(selected))return selected.getName();
        for(int id:InputDevice.getDeviceIds()){InputDevice d=InputDevice.getDevice(id);if(gamepad(d))return d.getName();}
        return "No controller connected";
    }
    private boolean select(int id){
        // Some USB controllers expose a silent physical device plus an active bridge.
        // Select on input, never merely the first device in Android's inventory.
        if(active==NO_DEVICE){active=id;Log.i("RuneController","Active device "+id+": "+status());}
        if(active==id&&gamepad(InputDevice.getDevice(id)))lastDevice=id;
        return active==id;
    }
    static int mapping(int k){
        switch(k){
            case KeyEvent.KEYCODE_BUTTON_A:return KeyEvent.KEYCODE_SPACE;
            case KeyEvent.KEYCODE_BUTTON_B:return KeyEvent.KEYCODE_C;
            case KeyEvent.KEYCODE_BUTTON_X:case KeyEvent.KEYCODE_BUTTON_R2:return KeyEvent.KEYCODE_ENTER;
            case KeyEvent.KEYCODE_BUTTON_Y:case KeyEvent.KEYCODE_BUTTON_THUMBL:return KeyEvent.KEYCODE_CTRL_LEFT;
            case KeyEvent.KEYCODE_BUTTON_L1:case KeyEvent.KEYCODE_BUTTON_L2:return KeyEvent.KEYCODE_CTRL_RIGHT;
            case KeyEvent.KEYCODE_BUTTON_R1:return KeyEvent.KEYCODE_Q;
            case KeyEvent.KEYCODE_BUTTON_THUMBR:return KeyEvent.KEYCODE_F;
            case KeyEvent.KEYCODE_BUTTON_SELECT:return KeyEvent.KEYCODE_E;
            case KeyEvent.KEYCODE_DPAD_LEFT:return KeyEvent.KEYCODE_1;
            case KeyEvent.KEYCODE_DPAD_UP:return KeyEvent.KEYCODE_2;
            case KeyEvent.KEYCODE_DPAD_RIGHT:return KeyEvent.KEYCODE_3;
            case KeyEvent.KEYCODE_DPAD_DOWN:return KeyEvent.KEYCODE_4;
            default:return 0;
        }
    }
    boolean key(KeyEvent e){
        int k=e.getKeyCode();
        if(!gamepad(e.getDevice())&&!KeyEvent.isGamepadButton(k))return false;
        if(active==NO_DEVICE&&e.getAction()!=KeyEvent.ACTION_DOWN)return true;
        if(!select(e.getDeviceId()))return true;
        if(e.getAction()==KeyEvent.ACTION_DOWN)buttons.add(k);else if(e.getAction()==KeyEvent.ACTION_UP)buttons.remove(k);
        if(trace&&e.getRepeatCount()==0)Log.i("RuneController","button device="+active+" code="+k+" down="+(e.getAction()==0)+" mapped="+mapping(k));
        publish();return true;
    }
    private void publish(){
        HashSet<Integer> wanted=new HashSet<>();
        for(int button:buttons){int k=mapping(button);if(k!=0)wanted.add(k);}
        if(leftTrigger)wanted.add(KeyEvent.KEYCODE_CTRL_RIGHT);
        if(rightTrigger)wanted.add(KeyEvent.KEYCODE_ENTER);
        if(hatX<-.5f)wanted.add(KeyEvent.KEYCODE_1);if(hatX>.5f)wanted.add(KeyEvent.KEYCODE_3);
        if(hatY<-.5f)wanted.add(KeyEvent.KEYCODE_2);if(hatY>.5f)wanted.add(KeyEvent.KEYCODE_4);
        for(int k:new HashSet<>(sent))if(!wanted.contains(k)){host.controllerKey(k,false);sent.remove(k);}
        for(int k:wanted)if(sent.add(k))host.controllerKey(k,true);
    }
    private static float axis(MotionEvent e,int primary,int fallback){
        InputDevice d=e.getDevice();return e.getAxisValue(d!=null&&d.getMotionRange(primary,e.getSource())!=null?primary:fallback);
    }
    private static float stick(MotionEvent e,int primary,int fallback){
        InputDevice d=e.getDevice();int a=d!=null&&d.getMotionRange(primary,e.getSource())!=null?primary:fallback;
        InputDevice.MotionRange r=d==null?null:d.getMotionRange(a,e.getSource());
        return RuneControllerMath.deadzone(e.getAxisValue(a),r==null?.16f:Math.max(.16f,r.getFlat()));
    }
    boolean motion(MotionEvent e){
        if((e.getSource()&InputDevice.SOURCE_JOYSTICK)!=InputDevice.SOURCE_JOYSTICK)return false;
        float x=stick(e,MotionEvent.AXIS_X,MotionEvent.AXIS_X),y=stick(e,MotionEvent.AXIS_Y,MotionEvent.AXIS_Y);
        float rx=stick(e,MotionEvent.AXIS_Z,MotionEvent.AXIS_RX),ry=stick(e,MotionEvent.AXIS_RZ,MotionEvent.AXIS_RY);
        float lt=axis(e,MotionEvent.AXIS_LTRIGGER,MotionEvent.AXIS_BRAKE),rt=axis(e,MotionEvent.AXIS_RTRIGGER,MotionEvent.AXIS_GAS);
        float hx=e.getAxisValue(MotionEvent.AXIS_HAT_X),hy=e.getAxisValue(MotionEvent.AXIS_HAT_Y);
        if(active==NO_DEVICE&&x==0&&y==0&&rx==0&&ry==0&&lt<.35f&&rt<.35f&&hx==0&&hy==0)return true;
        if(!select(e.getDeviceId()))return true;
        if(host.controllerMenu()){
            float nx=Math.abs(hx)>.5f?hx:x,ny=Math.abs(hy)>.5f?hy:y;
            int next=Math.max(Math.abs(nx),Math.abs(ny))<.5f?0:Math.abs(nx)>Math.abs(ny)?(nx<0?KeyEvent.KEYCODE_DPAD_LEFT:KeyEvent.KEYCODE_DPAD_RIGHT):(ny<0?KeyEvent.KEYCODE_DPAD_UP:KeyEvent.KEYCODE_DPAD_DOWN);
            if(nav!=next){handler.removeCallbacks(repeat);nav=next;if(nav!=0){host.controllerNavigate(nav);handler.postDelayed(repeat,350);}}
            return true;
        }
        hatX=hx;hatY=hy;
        leftTrigger=RuneControllerMath.trigger(lt,leftTrigger);rightTrigger=RuneControllerMath.trigger(rt,rightTrigger);
        publish();
        float speed=host.controllerSensitivity();
        host.controllerAxes(x,-y,rx*speed,-ry*speed*(host.controllerInvert()?-1:1));
        if(trace&&SystemClock.uptimeMillis()-lastTrace>500){lastTrace=SystemClock.uptimeMillis();Log.i("RuneController","axes device="+active+" move="+x+","+(-y)+" look="+rx+","+ry+" triggers="+lt+","+rt);}
        return true;
    }
    private final Runnable repeat=new Runnable(){public void run(){if(nav!=0&&host.controllerMenu()){host.controllerNavigate(nav);handler.postDelayed(this,120);}}};
    void reset(){
        handler.removeCallbacks(repeat);nav=0;buttons.clear();hatX=hatY=0;leftTrigger=rightTrigger=false;
        for(int k:sent)host.controllerKey(k,false);sent.clear();host.controllerAxes(0,0,0,0);active=NO_DEVICE;
    }
    void close(){reset();manager.unregisterInputDeviceListener(this);}
    public void onInputDeviceAdded(int id){refreshConnection();Log.i("RuneController","Device added: "+status());}
    public void onInputDeviceRemoved(int id){if(active==id)reset();if(lastDevice==id)lastDevice=NO_DEVICE;refreshConnection();Log.i("RuneController","Device removed: "+id);}
    public void onInputDeviceChanged(int id){if(active==id)reset();refreshConnection();}
}
