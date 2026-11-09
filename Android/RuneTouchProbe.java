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
        float moveX,moveY,lookX,lookY;int jumpPresses;final HashSet<Integer> keys=new HashSet<>();
        public void touchKey(int k,boolean down){if(down){if(keys.add(k)&&k==KeyEvent.KEYCODE_SPACE)jumpPresses++;}else keys.remove(k);}
        public void touchMove(float x,float y){moveX=x;moveY=y;}
        public void touchCamera(float x,float y){lookX=x;lookY=y;}
        public void touchLook(float x,float y){}
        public void touchMenu(){}public void closeTouchEditor(){}
    }
    static float[] point(RuneTouchView v,int id,float dx,float dy){View b=v.buttons[id];return new float[]{b.getLeft()+b.getWidth()*(.5f+dx),b.getTop()+b.getHeight()*(.5f+dy)};}
    static void event(RuneTouchView v,int action,int[] ids,float[]... xy){eventAt(v,SystemClock.uptimeMillis(),action,ids,xy);}
    static void eventAt(RuneTouchView v,long now,int action,int[] ids,float[]... xy){
        MotionEvent.PointerProperties[] props=new MotionEvent.PointerProperties[ids.length];MotionEvent.PointerCoords[] coords=new MotionEvent.PointerCoords[ids.length];
        for(int i=0;i<ids.length;i++){props[i]=new MotionEvent.PointerProperties();props[i].id=ids[i];props[i].toolType=MotionEvent.TOOL_TYPE_FINGER;coords[i]=new MotionEvent.PointerCoords();coords[i].x=xy[i][0];coords[i].y=xy[i][1];coords[i].pressure=1;coords[i].size=.1f;}
        MotionEvent e=MotionEvent.obtain(now,now,action,ids.length,props,coords,0,0,1,1,0,0,InputDevice.SOURCE_TOUCHSCREEN,0);
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
    static void slam(RuneTouchView v) {
        boolean shown=v.shown;
        if(v.editing)throw new AssertionError("Slam touch fixture cannot edit controls");
        // Isolated intent only: expose the actual views temporarily even when
        // a controller is attached; saved layout/preferences are never changed.
        if(!shown)v.configure(true,true);
        v.postDelayed(()->slamVisible(v,shown),100);
    }
    private static void slamVisible(RuneTouchView v,boolean shown) {
        float[] jump=point(v,RuneTouchLayout.JUMP,0,0),attack=point(v,RuneTouchLayout.ATTACK,0,0);
        event(v,MotionEvent.ACTION_DOWN,new int[]{0},jump);
        v.postDelayed(()->event(v,MotionEvent.ACTION_UP,new int[]{0},jump),40);
        v.postDelayed(()->event(v,MotionEvent.ACTION_DOWN,new int[]{0},jump),160);
        v.postDelayed(()->event(v,MotionEvent.ACTION_UP,new int[]{0},jump),200);
        v.postDelayed(()->event(v,MotionEvent.ACTION_DOWN,new int[]{0},attack),290);
        v.postDelayed(()->event(v,MotionEvent.ACTION_UP,new int[]{0},attack),340);
        v.postDelayed(()->{v.release();if(!shown)v.configure(false,true);},1000);
        android.util.Log.i("RuneTouchProbe","SLAM_TOUCH_DISPATCH jump-jump-attack");
    }
    static void icons(Context context,RuneTouchView v)throws java.io.IOException {
        int[] ids={RuneTouchLayout.USE,RuneTouchLayout.BLOCK,RuneTouchLayout.ATTACK,RuneTouchLayout.KICK,RuneTouchLayout.JUMP,RuneTouchLayout.THROW,RuneTouchLayout.DIVE,RuneTouchLayout.POWER};
        for(float size:new float[]{.5f,1,2}) {
            for(RuneTouchLayout.Item item:v.layout.items)item.size=size;
            int w=View.MeasureSpec.makeMeasureSpec(2520,View.MeasureSpec.EXACTLY),h=View.MeasureSpec.makeMeasureSpec(1680,View.MeasureSpec.EXACTLY);
            v.arrange();v.measure(w,h);v.layout(0,0,2520,1680);
            android.graphics.Bitmap atlas=android.graphics.Bitmap.createBitmap(800,400,android.graphics.Bitmap.Config.ARGB_8888);
            android.graphics.Canvas canvas=new android.graphics.Canvas(atlas);canvas.drawColor(0xff303438);
            for(int i=0;i<ids.length;i++) {
                View control=v.buttons[ids[i]];canvas.save();canvas.translate((i%4)*200+20,(i/4)*200+20);
                float scale=160f/Math.max(control.getWidth(),control.getHeight());canvas.scale(scale,scale);control.draw(canvas);canvas.restore();
            }
            try(java.io.FileOutputStream stream=new java.io.FileOutputStream(new java.io.File(context.getFilesDir(),"touch-icons-"+Math.round(size*100)+".png"))){atlas.compress(android.graphics.Bitmap.CompressFormat.PNG,100,stream);}
            atlas.recycle();
        }
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
        // Camera-center double taps produce one finite jump pulse, without
        // stealing movement or a separately held Jump button.
        float[] center=point(v,RuneTouchLayout.CAMERA,0,0),edge=point(v,RuneTouchLayout.CAMERA,.32f,0),jump=point(v,RuneTouchLayout.JUMP,0,0);
        long stamp=SystemClock.uptimeMillis()+1000;
        eventAt(v,stamp,0,new int[]{0},center);eventAt(v,stamp+40,1,new int[]{0},center);
        check(!sink.keys.contains(KeyEvent.KEYCODE_SPACE),"Single camera tap jumped");
        eventAt(v,stamp+120,0,new int[]{0},center);eventAt(v,stamp+160,1,new int[]{0},center);
        check(sink.keys.contains(KeyEvent.KEYCODE_SPACE)&&sink.jumpPresses==1,"Center double tap did not jump once");
        v.finishCameraJump.run();check(!sink.keys.contains(KeyEvent.KEYCODE_SPACE),"Camera jump pulse stuck");
        // Camera movement, even when the finger returns to center, is not a tap.
        eventAt(v,stamp+300,0,new int[]{0},center);eventAt(v,stamp+340,2,new int[]{0},edge);eventAt(v,stamp+380,1,new int[]{0},center);
        eventAt(v,stamp+440,0,new int[]{0},center);eventAt(v,stamp+480,1,new int[]{0},center);
        check(sink.jumpPresses==1,"Camera drag completed a double tap");v.release();
        eventAt(v,stamp+600,0,new int[]{0},edge);eventAt(v,stamp+640,1,new int[]{0},edge);
        eventAt(v,stamp+700,0,new int[]{0},edge);eventAt(v,stamp+740,1,new int[]{0},edge);
        check(sink.jumpPresses==1,"Off-center camera taps jumped");
        eventAt(v,stamp+800,0,new int[]{0},center);eventAt(v,stamp+1100,1,new int[]{0},center);
        eventAt(v,stamp+1160,0,new int[]{0},center);eventAt(v,stamp+1200,1,new int[]{0},center);
        check(sink.jumpPresses==1,"Long press completed a double tap");v.release();
        eventAt(v,stamp+1300,0,new int[]{0},center);eventAt(v,stamp+1340,1,new int[]{0},center);
        eventAt(v,stamp+1800,0,new int[]{0},center);eventAt(v,stamp+1840,1,new int[]{0},center);
        check(sink.jumpPresses==1,"Slow camera taps jumped");v.release();
        eventAt(v,stamp+1900,0,new int[]{0},center);eventAt(v,stamp+1940,3,new int[]{0},center);
        eventAt(v,stamp+2000,0,new int[]{0},center);eventAt(v,stamp+2040,1,new int[]{0},center);
        check(sink.jumpPresses==1,"Cancelled camera tap jumped");v.release();
        // Simultaneous left-stick movement remains held while right taps jump.
        eventAt(v,stamp+2100,0,new int[]{0},left);
        eventAt(v,stamp+2140,261,new int[]{0,1},left,center);eventAt(v,stamp+2180,262,new int[]{0,1},left,center);
        eventAt(v,stamp+2240,261,new int[]{0,1},left,center);eventAt(v,stamp+2280,262,new int[]{0,1},left,center);
        check(sink.moveY>.5f&&sink.keys.contains(KeyEvent.KEYCODE_SPACE)&&sink.jumpPresses==2,"Camera jump interrupted left stick");
        v.finishCameraJump.run();eventAt(v,stamp+2320,1,new int[]{0},left);v.release();
        // Pulse expiry must not release the other button's existing held jump.
        eventAt(v,stamp+2400,0,new int[]{0},jump);
        eventAt(v,stamp+2440,261,new int[]{0,1},jump,center);eventAt(v,stamp+2480,262,new int[]{0,1},jump,center);
        eventAt(v,stamp+2540,261,new int[]{0,1},jump,center);eventAt(v,stamp+2580,262,new int[]{0,1},jump,center);
        v.finishCameraJump.run();check(sink.keys.contains(KeyEvent.KEYCODE_SPACE),"Camera pulse released held Jump button");
        eventAt(v,stamp+2620,1,new int[]{0},jump);check(sink.keys.isEmpty(),"Shared jump release stuck");v.release();
        // Hiding controls cancels both a pending tap pair and a pending pulse.
        eventAt(v,stamp+2700,0,new int[]{0},center);eventAt(v,stamp+2740,1,new int[]{0},center);
        v.configure(false,true);v.configure(true,true);
        int before=sink.jumpPresses;eventAt(v,stamp+2800,0,new int[]{0},center);eventAt(v,stamp+2840,1,new int[]{0},center);
        check(sink.jumpPresses==before,"Hidden tap history survived");
        eventAt(v,stamp+2900,0,new int[]{0},center);eventAt(v,stamp+2940,1,new int[]{0},center);
        check(sink.keys.contains(KeyEvent.KEYCODE_SPACE),"Final camera pulse missing");v.configure(false,true);
        check(sink.keys.isEmpty()&&!v.cameraJumpHeld,"Hide left camera jump held");v.configure(true,true);v.release();
        // Two fingers on one action must not release it until the last finger lifts.
        event(v,0,new int[]{0},hit);event(v,261,new int[]{0,1},hit,hit);event(v,6,new int[]{0,1},hit,hit);
        check(sink.keys.contains(KeyEvent.KEYCODE_ENTER),"First finger release cleared second finger");event(v,1,new int[]{1},hit);check(sink.keys.isEmpty(),"Last finger release stuck");
        // Crouch and dive are held actions, not taps. A second finger, cancel,
        // or hiding controls must never leave the shared C/Duck command stuck.
        float[] duck=point(v,RuneTouchLayout.DIVE,0,0);
        event(v,0,new int[]{0},duck);check(sink.keys.contains(KeyEvent.KEYCODE_C),"Crouch / Dive press missing");
        event(v,261,new int[]{0,1},duck,duck);event(v,6,new int[]{0,1},duck,duck);
        check(sink.keys.contains(KeyEvent.KEYCODE_C),"Crouch released before final finger");
        event(v,1,new int[]{1},duck);check(!sink.keys.contains(KeyEvent.KEYCODE_C),"Crouch / Dive release stuck");
        event(v,0,new int[]{0},duck);event(v,3,new int[]{0},duck);check(sink.keys.isEmpty(),"Crouch / Dive cancel stuck");
        event(v,0,new int[]{0},duck);v.configure(false,true);check(sink.keys.isEmpty(),"Hidden crouch remained held");v.configure(true,true);
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
        v.release();icons(c,v);android.util.Log.i("RuneTouchProbe","PASS: camera-center jump/drag/timing/shared-release, icons exported,  crouch/dive press/hold/release/cancel/hide, simultaneous sticks/attack, independent release, cancel, two fingers per action, hide releases inputs, Menu retained, disconnect restores controls");
    }catch(Throwable e){android.util.Log.e("RuneTouchProbe","FAIL: Android touch dispatch regression",e);}}
}
