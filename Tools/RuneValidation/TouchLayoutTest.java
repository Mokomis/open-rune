package org.runeclassic.port;
import android.content.SharedPreferences;
import java.util.HashMap;
import java.util.Map;
public final class TouchLayoutTest {
    static void check(boolean ok,String msg){if(!ok)throw new AssertionError(msg);}
    public static void main(String[] args){
        Map<String,Object> disk=new HashMap<>();disk.put("menu.opacity",3);disk.put("hud.health.size",1.4f);
        SharedPreferences prefs=HudLayoutTest.prefs(disk);RuneTouchLayout saved=RuneTouchLayout.load(prefs),draft=saved.copy();
        check(RuneTouchLayout.COUNT==12 && RuneTouchLayout.NAMES[RuneTouchLayout.POWER].equals("Rune Power"),"Rune Power control missing");
        check(saved.items[4].opacity==.8f,"Existing opacity was not migrated");
        draft.items[4].size=1.5f;draft.items[4].opacity=.4f;draft.move(4,700,300,1600,900);
        check(saved.items[4].size==1&&disk.size()==2,"Editing/Cancel changes saved layout");
        draft.save(prefs);RuneTouchLayout r=RuneTouchLayout.load(prefs);
        check(r.items[4].size==1.5f&&r.items[4].opacity==.4f&&Math.abs(r.left(4,1600,900)-700)<.01,"Restart loses independent properties");
        check(r.items[3].size==1&&disk.get("hud.health.size").equals(1.4f),"Touch edits alter other controls/HUD");
        for(float w:new float[]{800,1600,2400,3200})for(float h:new float[]{500,900,1067})for(int i=0;i<RuneTouchLayout.COUNT;i++){
            r.items[i].size=2;r.move(i,99999,-99999,w,h);float s=r.scale(i,w,h),x=r.left(i,w,h),y=r.top(i,w,h);
            check(x>=15.99&&y>=15.99&&x+RuneTouchLayout.WIDTH[i]*s<=w-15.99&&y+RuneTouchLayout.HEIGHT[i]*s<=h-15.99,"Control escapes viewport after aspect/size change");
        }
        disk.put("touchLayout.attack.size",Float.NaN);disk.put("touchLayout.menu.x",Float.POSITIVE_INFINITY);disk.put("touchLayout.dive.opacity",Float.NEGATIVE_INFINITY);
        r=RuneTouchLayout.load(prefs);check(r.items[4].size==1&&Float.isFinite(r.items[1].x)&&r.items[8].opacity>=.2,"Invalid saved values escape sanitization");
        r=new RuneTouchLayout();
        for(int i=0;i<RuneTouchLayout.COUNT;i++)for(int j=i+1;j<RuneTouchLayout.COUNT;j++){
            if(i==9||j==9)continue;
            float x=r.left(i,1600,900),y=r.top(i,1600,900),xx=r.left(j,1600,900),yy=r.top(j,1600,900);
            check(x+RuneTouchLayout.WIDTH[i]<=xx||xx+RuneTouchLayout.WIDTH[j]<=x||y+RuneTouchLayout.HEIGHT[i]<=yy||yy+RuneTouchLayout.HEIGHT[j]<=y,"Default controls overlap: "+i+"/"+j);
        }
        System.out.println("PASS: draft isolation, migration, persistence, independent size/opacity, viewport bounds, invalid values, default non-overlap");
    }
}
