package org.runeclassic.port;

import android.content.SharedPreferences;

/** Touch transforms use free-space anchors so resizing/aspect changes stay on screen. */
final class RuneTouchLayout {
    static final int MOVE=0,MENU=1,USE=2,BLOCK=3,ATTACK=4,KICK=5,JUMP=6,THROW=7,DIVE=8,LOOK=9,CAMERA=10,POWER=11,COUNT=12;
    static final String[] NAMES={"Move","Menu","Use","Block","Attack","Kick","Jump","Throw","Dive","Look area","Camera","Rune Power"};
    static final String[] KEYS={"move","menu","use","block","attack","kick","jump","throw","dive","look","camera","power"};
    static final float[] WIDTH={260,132,110,125,150,110,110,110,110,800,220,116};
    static final float[] HEIGHT={260,52,110,125,150,110,110,110,110,750,220,116};
    static final class Item {
        float x,y,size=1,opacity=.65f;
        Item copy(){Item i=new Item();i.x=x;i.y=y;i.size=size;i.opacity=opacity;return i;}
    }
    final Item[] items=new Item[COUNT];
    RuneTouchLayout(){for(int i=0;i<COUNT;i++)reset(i);}
    static float clamp(float n,float lo,float hi){return Math.max(lo,Math.min(hi,n));}
    static float finite(float n,float fallback){return Float.isFinite(n)?n:fallback;}
    void reset(int i){
        Item a=new Item();items[i]=a;
        float[] x={.015f,.015f,.205f,.985f,.815f,.82f,.985f,.80f,.892f,1,.985f,.68f};
        float[] y={.92f,.60f,.95f,.50f,.81f,.55f,.995f,.995f,.995f,.5f,.80f,.55f};
        a.x=x[i];a.y=y[i];if(i==MENU)a.opacity=.9f;
    }
    RuneTouchLayout copy(){RuneTouchLayout l=new RuneTouchLayout();for(int i=0;i<COUNT;i++)l.items[i]=items[i].copy();return l;}
    float scale(int i,float w,float h){return Math.max(.01f,Math.min(clamp(finite(items[i].size,1),.5f,2),Math.min((w-32)/WIDTH[i],(h-32)/HEIGHT[i])));}
    float left(int i,float w,float h){return 16+clamp(items[i].x,0,1)*Math.max(0,w-32-WIDTH[i]*scale(i,w,h));}
    float top(int i,float w,float h){return 16+clamp(items[i].y,0,1)*Math.max(0,h-32-HEIGHT[i]*scale(i,w,h));}
    void move(int i,float x,float y,float w,float h){items[i].x=clamp((x-16)/Math.max(1,w-32-WIDTH[i]*scale(i,w,h)),0,1);items[i].y=clamp((y-16)/Math.max(1,h-32-HEIGHT[i]*scale(i,w,h)),0,1);}
    void save(SharedPreferences p){SharedPreferences.Editor e=p.edit().putInt("touchLayout.version",1);for(int i=0;i<COUNT;i++){String k="touchLayout."+KEYS[i]+".";Item a=items[i];e.putFloat(k+"x",a.x).putFloat(k+"y",a.y).putFloat(k+"size",a.size).putFloat(k+"opacity",a.opacity);}e.apply();}
    static RuneTouchLayout load(SharedPreferences p){RuneTouchLayout l=new RuneTouchLayout();float old=new float[]{.35f,.5f,.65f,.8f,1}[Math.max(0,Math.min(4,p.getInt("menu.opacity",2)))];for(int i=0;i<COUNT;i++){String k="touchLayout."+KEYS[i]+".";Item a=l.items[i];a.x=clamp(finite(p.getFloat(k+"x",a.x),a.x),0,1);a.y=clamp(finite(p.getFloat(k+"y",a.y),a.y),0,1);a.size=clamp(finite(p.getFloat(k+"size",1),1),.5f,2);a.opacity=clamp(finite(p.getFloat(k+"opacity",i==MENU?.9f:old),old),.2f,1);}return l;}
}
