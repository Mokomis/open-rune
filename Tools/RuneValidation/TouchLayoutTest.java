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
        int afterMigration=disk.size();
        draft.items[4].size=1.5f;draft.items[4].opacity=.4f;draft.move(4,700,300,1600,900);
        check(saved.items[4].size==1&&disk.size()==afterMigration,"Editing/Cancel changes saved layout");
        draft.save(prefs);RuneTouchLayout r=RuneTouchLayout.load(prefs);
        check(r.items[4].size==1.5f&&r.items[4].opacity==.4f&&Math.abs(r.left(4,1600,900)-700)<.01,"Restart loses independent properties");
        check(r.items[3].size==1&&disk.get("hud.health.size").equals(1.4f),"Touch edits alter other controls/HUD");
        for(float w:new float[]{800,1600,2400,3200})for(float h:new float[]{500,900,1067})for(int i=0;i<RuneTouchLayout.COUNT;i++){
            r.items[i].size=2;r.move(i,99999,-99999,w,h);float s=r.scale(i,w,h),x=r.left(i,w,h),y=r.top(i,w,h);
            check(x>=15.99&&y>=15.99&&x+RuneTouchLayout.WIDTH[i]*s<=w-15.99&&y+RuneTouchLayout.HEIGHT[i]*s<=h-15.99,"Control escapes viewport after aspect/size change");
        }
        disk.put("touchLayout.attack.size",Float.NaN);disk.put("touchLayout.menu.x",Float.POSITIVE_INFINITY);disk.put("touchLayout.dive.opacity",Float.NEGATIVE_INFINITY);
        r=RuneTouchLayout.load(prefs);check(r.items[4].size==1&&Float.isFinite(r.items[1].x)&&r.items[8].opacity>=.2,"Invalid saved values escape sanitization");
        Map<String,Object> legacy=new HashMap<>();legacy.put("touchLayout.version",1);legacy.put("touchLayout.attack.x",.42f);legacy.put("touchLayout.attack.size",1.4f);legacy.put("menu.aa",2);
        SharedPreferences previous=HudLayoutTest.prefs(legacy);RuneTouchLayout migrated=RuneTouchLayout.load(previous);
        check(legacy.get("touchLayout.previous.attack.x").equals(.42f)&&legacy.get("touchLayout.previous.attack.size").equals(1.4f),"Previous layout was not retained");
        check(migrated.items[4].x==-1&&migrated.items[4].size==1&&legacy.get("menu.aa").equals(2),"Migration changes unrelated settings or misses new defaults");
        migrated.move(4,700,300,1600,900);migrated.save(previous);
        check(Math.abs(RuneTouchLayout.load(previous).left(4,1600,900)-700)<.01,"Migration repeats over a new custom layout");
        r=new RuneTouchLayout();
        for(int i=0;i<RuneTouchLayout.COUNT;i++)for(int j=i+1;j<RuneTouchLayout.COUNT;j++){
            if(i==9||j==9)continue;
            float x=r.left(i,1600,900),y=r.top(i,1600,900),xx=r.left(j,1600,900),yy=r.top(j,1600,900);
            check(x+RuneTouchLayout.WIDTH[i]<=xx||xx+RuneTouchLayout.WIDTH[j]<=x||y+RuneTouchLayout.HEIGHT[i]<=yy||yy+RuneTouchLayout.HEIGHT[j]<=y,"Default controls overlap: "+i+"/"+j);
        }
        System.out.println("PASS: draft isolation, migration, persistence, independent size/opacity, viewport bounds, invalid values, default non-overlap");
    }
}
