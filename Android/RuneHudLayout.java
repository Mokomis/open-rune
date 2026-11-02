package org.runeclassic.port;

import android.content.SharedPreferences;

/** Versioned, independent HUD transforms. Normalized free-space anchors keep
 * every element reachable after orientation, aspect ratio or size changes. */
final class RuneHudLayout {
    static final String[] NAMES={"Health","Shield","Rune power","Bloodlust","Air","Messages","FPS","Score / time"};
    static final String[] KEYS={"health","shield","power","bloodlust","air","messages","fps","score"};
    static final int COUNT=8;
    static final class Item {
        float x,y,size=1;
        boolean horizontal;
        Item copy(){Item a=new Item();a.x=x;a.y=y;a.size=size;a.horizontal=horizontal;return a;}
    }
    final Item[] items=new Item[COUNT];
    RuneHudLayout(){for(int i=0;i<COUNT;i++)reset(i);}
    RuneHudLayout copy(){RuneHudLayout a=new RuneHudLayout();for(int i=0;i<COUNT;i++)a.items[i]=items[i].copy();return a;}
    void reset(int i){
        Item a=new Item();items[i]=a;
        a.horizontal=i>=3;
        float[] xs={.02f,.077f,.98f,.5f,.5f,.5f,.5f,.98f};
        float[] ys={.07f,.07f,.07f,.95f,.88f,.065f,.015f,.015f};
        a.x=xs[i];a.y=ys[i];
    }
    void preset(boolean horizontal){
        for(int i=0;i<COUNT;i++)reset(i);
        for(int i=0;i<3;i++)items[i].horizontal=horizontal;
        if(horizontal){items[1].x=.02f;items[1].y=.135f;}
    }
    static float finite(float n,float fallback){return Float.isNaN(n)||Float.isInfinite(n)?fallback:n;}
    static float clamp(float n,float lo,float hi){return Math.max(lo,Math.min(hi,n));}
    float width(int i){return i==5?360:i==6?320:i==7?180:items[i].horizontal?(i==3||i==4?330:360):i==2?88:80;}
    float height(int i){return i==5?100:i>=6?32:items[i].horizontal?50:i==0?255:i==1?210:240;}
    float scale(int i,float w,float h){return Math.min(clamp(finite(items[i].size,1),.5f,2f),Math.min((w-20)/width(i),(h-20)/height(i)));}
    float left(int i,float w,float h){return 10+clamp(items[i].x,0,1)*Math.max(0,w-20-width(i)*scale(i,w,h));}
    float top(int i,float w,float h){return 10+clamp(items[i].y,0,1)*Math.max(0,h-20-height(i)*scale(i,w,h));}
    void move(int i,float x,float y,float w,float h){
        items[i].x=clamp((x-10)/Math.max(1,w-20-width(i)*scale(i,w,h)),0,1);
        items[i].y=clamp((y-10)/Math.max(1,h-20-height(i)*scale(i,w,h)),0,1);
    }
    void save(SharedPreferences prefs){
        SharedPreferences.Editor e=prefs.edit().putInt("hud.version",1);
        for(int i=0;i<COUNT;i++){
            String k="hud."+KEYS[i]+".";Item a=items[i];
            e.putFloat(k+"x",a.x).putFloat(k+"y",a.y).putFloat(k+"size",a.size).putBoolean(k+"horizontal",a.horizontal);
        }
        e.apply();
    }
    static RuneHudLayout load(SharedPreferences prefs){
        RuneHudLayout l=new RuneHudLayout();
        for(int i=0;i<COUNT;i++){
            String k="hud."+KEYS[i]+".";Item a=l.items[i];
            a.x=clamp(finite(prefs.getFloat(k+"x",a.x),a.x),0,1);
            a.y=clamp(finite(prefs.getFloat(k+"y",a.y),a.y),0,1);
            a.size=clamp(finite(prefs.getFloat(k+"size",1),1),.5f,2);
            a.horizontal=prefs.getBoolean(k+"horizontal",a.horizontal);
        }
        return l;
    }
}
