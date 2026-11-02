package org.runeclassic.port;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.SystemClock;
import android.view.*;
import java.util.HashSet;

/** Opt-in isolated Android View dispatch regression. Never injects engine input. */
final class RuneTouchProbe {
    static void check(boolean b,String s){if(!b)throw new AssertionError(s);}
    static final class Sink implements RuneTouchView.Host {
        float moveX,moveY,lookX,lookY;final HashSet<Integer> keys=new HashSet<>();
        public void touchKey(int k,boolean down){if(down)keys.add(k);else keys.remove(k);}
        public void touchMove(float x,float y){moveX=x;moveY=y;}
        public void touchCamera(float x,float y){lookX=x;lookY=y;}
        public void touchLook(float x,float y){}
        public void touchMenu(){}public void closeTouchEditor(){}
    }
    static float[] point(RuneTouchView v,int id,float dx,float dy){View b=v.buttons[id];return new float[]{b.getLeft()+b.getWidth()*(.5f+dx),b.getTop()+b.getHeight()*(.5f+dy)};}
    static void event(RuneTouchView v,int action,int[] ids,float[]... xy){
        MotionEvent.PointerProperties[] props=new MotionEvent.PointerProperties[ids.length];MotionEvent.PointerCoords[] coords=new MotionEvent.PointerCoords[ids.length];
        for(int i=0;i<ids.length;i++){props[i]=new MotionEvent.PointerProperties();props[i].id=ids[i];props[i].toolType=MotionEvent.TOOL_TYPE_FINGER;coords[i]=new MotionEvent.PointerCoords();coords[i].x=xy[i][0];coords[i].y=xy[i][1];coords[i].pressure=1;coords[i].size=.1f;}
        long now=SystemClock.uptimeMillis();MotionEvent e=MotionEvent.obtain(now,now,action,ids.length,props,coords,0,0,1,1,0,0,InputDevice.SOURCE_TOUCHSCREEN,0);
        try{check(v.dispatchTouchEvent(e),"Dispatch did not consume "+action);}finally{e.recycle();}
    }
    // Opt-in integration fixture: dispatch through the visible touch views and
    // their real host. No direct engine-key or move invocation bypasses the UI.
    static void bloodlust(RuneTouchView v,String gesture){
        if(!v.shown||v.editing)throw new AssertionError("Bloodlust test requires visible controls");
        if(gesture.equals("leap-repeat")){
            bloodlust(v,"leap");
            v.postDelayed(()->bloodlust(v,"leap"),5000);
            v.postDelayed(()->bloodlust(v,"leap"),10000);
            return;
        }
        if(gesture.equals("leap")){
            float[] jump=point(v,RuneTouchLayout.JUMP,0,0);
            event(v,MotionEvent.ACTION_DOWN,new int[]{0},jump);
            v.postDelayed(()->event(v,MotionEvent.ACTION_UP,new int[]{0},jump),40);
            v.postDelayed(()->event(v,MotionEvent.ACTION_DOWN,new int[]{0},jump),160);
            v.postDelayed(()->event(v,MotionEvent.ACTION_UP,new int[]{0},jump),200);
        }else if(gesture.equals("charge")){
            float[] block=point(v,RuneTouchLayout.BLOCK,0,0),attack=point(v,RuneTouchLayout.ATTACK,0,0);
            event(v,MotionEvent.ACTION_DOWN,new int[]{0},block);
            v.postDelayed(()->event(v,261,new int[]{0,1},block,attack),150);
            v.postDelayed(()->event(v,262,new int[]{0,1},block,attack),230);
            v.postDelayed(()->event(v,MotionEvent.ACTION_UP,new int[]{0},block),900);
        }else throw new AssertionError("Unknown bloodlust gesture");
        android.util.Log.i("RuneTouchProbe","BLOODLUST_TOUCH_DISPATCH "+gesture);
    }
    static void run(Context c,SharedPreferences prefs){try{
        Sink sink=new Sink();RuneTouchView v=new RuneTouchView(c,prefs,sink);v.layout=new RuneTouchLayout();
        int w=View.MeasureSpec.makeMeasureSpec(2520,View.MeasureSpec.EXACTLY),h=View.MeasureSpec.makeMeasureSpec(1680,View.MeasureSpec.EXACTLY);
        v.measure(w,h);v.layout(0,0,2520,1680);v.arrange();v.measure(w,h);v.layout(0,0,2520,1680);
        float[] left=point(v,0,0,-.3f),right=point(v,10,.3f,0),hit=point(v,4,0,0);
        event(v,0,new int[]{0},left);event(v,261,new int[]{0,1},left,right);event(v,517,new int[]{0,1,2},left,right,hit);
        check(sink.moveY>.5f&&sink.lookX>.5f&&sink.keys.contains(KeyEvent.KEYCODE_ENTER),"Move/look/attack did not coexist");
        event(v,6,new int[]{0,1,2},left,right,hit);
        check(sink.moveX==0&&sink.moveY==0&&sink.lookX>.5f&&sink.keys.contains(KeyEvent.KEYCODE_ENTER),"Independent movement release failed");
        event(v,262,new int[]{1,2},right,hit);check(sink.keys.isEmpty()&&sink.lookX>.5f,"Action release cleared camera");
        event(v,3,new int[]{1},right);check(sink.lookX==0&&sink.lookY==0,"Cancel left camera held");
        // Two fingers on one action must not release it until the last finger lifts.
        event(v,0,new int[]{0},hit);event(v,261,new int[]{0,1},hit,hit);event(v,6,new int[]{0,1},hit,hit);
        check(sink.keys.contains(KeyEvent.KEYCODE_ENTER),"First finger release cleared second finger");event(v,1,new int[]{1},hit);check(sink.keys.isEmpty(),"Last finger release stuck");
        float[] rune=point(v,RuneTouchLayout.POWER,0,0);
        event(v,0,new int[]{0},rune);check(sink.keys.contains(KeyEvent.KEYCODE_E),"Rune Power touch did not press E");
        event(v,1,new int[]{0},rune);check(sink.keys.isEmpty(),"Rune Power touch remained held");
        event(v,0,new int[]{0},left);event(v,261,new int[]{0,1},left,right);event(v,517,new int[]{0,1,2},left,right,hit);
        v.configure(false,true);check(sink.moveX==0&&sink.moveY==0&&sink.lookX==0&&sink.lookY==0&&sink.keys.isEmpty(),"Connection/hide did not release all inputs");
        for(int i=0;i<RuneTouchLayout.COUNT;i++)check(v.buttons[i].getVisibility()==(i==1?View.VISIBLE:View.GONE),"Hidden gameplay controls or Menu visibility failed");
        v.setBloodlustMoves(3);check(!v.bloodlustCue(6)&&!v.bloodlustCue(3)&&!v.bloodlustCue(4),"Hidden controls flashed");
        v.configure(true,true);v.setBloodlustMoves(1);check(v.bloodlustCue(6)&&!v.bloodlustCue(3)&&!v.bloodlustCue(4),"Leap-only cue");
        v.setBloodlustMoves(3);check(v.bloodlustCue(6)&&v.bloodlustCue(3)&&v.bloodlustCue(4),"Shield combo cue");
        v.setBloodlustMoves(0);check(!v.bloodlustCue(6)&&!v.bloodlustCue(3)&&!v.bloodlustCue(4),"Unavailable cue");
        for(View b:v.buttons)check(b.getVisibility()==View.VISIBLE,"Disconnect did not restore control");
        v.release();android.util.Log.i("RuneTouchProbe","PASS: simultaneous sticks/attack, independent release, cancel, two fingers per action, hide releases inputs, Menu retained, disconnect restores controls");
    }catch(Throwable e){android.util.Log.e("RuneTouchProbe","FAIL: Android touch dispatch regression",e);}}
}
