package org.runeclassic.port;

import android.content.SharedPreferences;

/** Touch transforms use free-space anchors so resizing/aspect changes stay on screen. */
final class RuneTouchLayout {
    static final int MOVE=0,MENU=1,USE=2,BLOCK=3,ATTACK=4,KICK=5,JUMP=6,THROW=7,DIVE=8,LOOK=9,CAMERA=10,POWER=11,COUNT=12;
    static final String[] NAMES={"Move","Menu","Use","Block","Attack","Kick","Jump","Throw","Crouch / Dive","Look area","Camera","Rune Power"};
    static final String[] KEYS={"move","menu","use","block","attack","kick","jump","throw","dive","look","camera","power"};
    static final float[] WIDTH={230,130,105,110,120,120,120,105,110,800,230,105};
    static final float[] HEIGHT={230,60,105,110,120,120,120,105,110,750,230,105};
    static final class Item {
        float x=-1,y=-1,size=1,opacity=.65f;
        Item copy(){Item i=new Item();i.x=x;i.y=y;i.size=size;i.opacity=opacity;return i;}
    }
    final Item[] items=new Item[COUNT];
    RuneTouchLayout(){for(int i=0;i<COUNT;i++)reset(i);}
    static float clamp(float n,float lo,float hi){return Math.max(lo,Math.min(hi,n));}
    static float finite(float n,float fallback){return Float.isFinite(n)?n:fallback;}
    void reset(int i){
        Item a=new Item();items[i]=a;
        if(i==MENU)a.opacity=.9f;
    }
    RuneTouchLayout copy(){RuneTouchLayout l=new RuneTouchLayout();for(int i=0;i<COUNT;i++)l.items[i]=items[i].copy();return l;}
    float scale(int i,float w,float h){return Math.max(.01f,Math.min(clamp(finite(items[i].size,1),.5f,2),Math.min((w-32)/WIDTH[i],(h-32)/HEIGHT[i])));}
    // The Unity prototype's edge-anchored cluster, expressed in 1080-high units.
    // Negative anchors mean the responsive default, not an off-screen position.
    float defaultLeft(int i,float w){float[] x={55,w-155,w-560,w-130,w-420,w-420,w-270,w-560,w-130,w-800,w-285,w-705};return x[i];}
    float defaultTop(int i,float h){float[] y={h-340,24,h-270,h-580,h-380,h-230,h-520,h-420,h-450,h-910,h-340,h-270};return y[i];}
    float left(int i,float w,float h){float max=Math.max(16,w-16-WIDTH[i]*scale(i,w,h));return items[i].x<0?clamp(defaultLeft(i,w),16,max):16+clamp(items[i].x,0,1)*(max-16);}
    float top(int i,float w,float h){float max=Math.max(16,h-16-HEIGHT[i]*scale(i,w,h));return items[i].y<0?clamp(defaultTop(i,h),16,max):16+clamp(items[i].y,0,1)*(max-16);}
    void move(int i,float x,float y,float w,float h){items[i].x=clamp((x-16)/Math.max(1,w-32-WIDTH[i]*scale(i,w,h)),0,1);items[i].y=clamp((y-16)/Math.max(1,h-32-HEIGHT[i]*scale(i,w,h)),0,1);}
    void save(SharedPreferences p){SharedPreferences.Editor e=p.edit().putInt("touchLayout.version",2);for(int i=0;i<COUNT;i++){String k="touchLayout."+KEYS[i]+".";Item a=items[i];e.putFloat(k+"x",a.x).putFloat(k+"y",a.y).putFloat(k+"size",a.size).putFloat(k+"opacity",a.opacity);}e.apply();}
    static void migrate(SharedPreferences p){
        if(p.getInt("touchLayout.version",0)>=2)return;
        SharedPreferences.Editor e=p.edit();
        for(String key:KEYS)for(String field:new String[]{"x","y","size"}){
            String k="touchLayout."+key+"."+field;
            if(p.contains(k))e.putFloat("touchLayout.previous."+key+"."+field,p.getFloat(k,field.equals("size")?1:-1));
            e.remove(k);
        }
        // Only the requested control layout changes. Opacity and other preferences stay intact.
        e.putInt("touchLayout.version",2).apply();
    }
    static RuneTouchLayout load(SharedPreferences p){migrate(p);RuneTouchLayout l=new RuneTouchLayout();float old=new float[]{.35f,.5f,.65f,.8f,1}[Math.max(0,Math.min(4,p.getInt("menu.opacity",2)))];for(int i=0;i<COUNT;i++){String k="touchLayout."+KEYS[i]+".";Item a=l.items[i];a.x=clamp(finite(p.getFloat(k+"x",-1),-1),-1,1);a.y=clamp(finite(p.getFloat(k+"y",-1),-1),-1,1);a.size=clamp(finite(p.getFloat(k+"size",1),1),.5f,2);a.opacity=clamp(finite(p.getFloat(k+"opacity",i==MENU?.9f:old),old),.2f,1);}return l;}
}
