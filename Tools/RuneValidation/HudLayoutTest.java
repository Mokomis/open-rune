package org.runeclassic.port;
import android.content.SharedPreferences;
import java.lang.reflect.Proxy;
import java.util.HashMap;
import java.util.Map;
public final class HudLayoutTest {
    static void check(boolean value,String why){if(!value)throw new AssertionError(why);}
    static SharedPreferences prefs(Map<String,Object> disk){return (SharedPreferences)Proxy.newProxyInstance(HudLayoutTest.class.getClassLoader(),new Class[]{SharedPreferences.class},(p,m,a)->{
        if(m.getName().startsWith("get"))return disk.getOrDefault((String)a[0],a[1]);
        if(m.getName().equals("edit"))return Proxy.newProxyInstance(HudLayoutTest.class.getClassLoader(),new Class[]{SharedPreferences.Editor.class},(e,n,b)->{if(n.getName().startsWith("put")){disk.put((String)b[0],b[1]);return e;}return null;});
        throw new UnsupportedOperationException(m.getName());
    });}
    public static void main(String[] args){
        Map<String,Object> disk=new HashMap<>();disk.put("menu.aa",2);SharedPreferences p=prefs(disk);
        RuneHudLayout original=RuneHudLayout.load(p),draft=original.copy();
        draft.items[0].size=1.7f;draft.items[0].horizontal=true;draft.move(0,700,500,1536,1024);
        check(original.items[0].size==1&&!original.items[0].horizontal,"Draft changes leak before Save");
        check(disk.size()==1,"Draft/Cancel writes preferences");
        draft.save(p);RuneHudLayout reloaded=RuneHudLayout.load(p);
        check(reloaded.items[0].size==1.7f&&reloaded.items[0].horizontal,"Size/orientation lost on restart");
        check(Math.abs(reloaded.left(0,1536,1024)-700)<.01,"Position lost on restart");
        check(reloaded.items[1].size==1,"Resizing Health changes Shield");
        check(disk.get("menu.aa").equals(2),"HUD save changes video settings");
        for(float w:new float[]{640,1536,2400})for(float h:new float[]{480,1024,1080})for(int i=0;i<RuneHudLayout.COUNT;i++){
            reloaded.items[i].size=2;reloaded.move(i,99999,-99999,w,h);
            float x=reloaded.left(i,w,h),y=reloaded.top(i,w,h),s=reloaded.scale(i,w,h);
            check(x>=9.99&&y>=9.99&&x+reloaded.width(i)*s<=w-9.99&&y+reloaded.height(i)*s<=h-9.99,"Offscreen element after resize/aspect change");
        }
        disk.put("hud.health.size",Float.NaN);disk.put("hud.health.x",Float.POSITIVE_INFINITY);
        reloaded=RuneHudLayout.load(p);check(reloaded.items[0].size==1&&Float.isFinite(reloaded.items[0].x),"Corrupt values poison layout");
        reloaded.preset(true);check(reloaded.items[0].horizontal&&reloaded.items[1].y>reloaded.items[0].y,"Horizontal preset overlap");
        reloaded.reset(0);check(!reloaded.items[0].horizontal&&reloaded.items[1].horizontal,"Reset element resets other elements");
        System.out.println("PASS: draft isolation, persistence, independent sizing, bounds/aspect ratio, corrupt preference recovery and presets");
    }
}
