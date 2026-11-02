package org.runeclassic.port;

import android.content.SharedPreferences;
import java.lang.reflect.Proxy;
import java.util.HashMap;
import java.util.Map;

/** Host-side model checks. No Android runtime or game assets required. */
public final class MenuSettingsTest {
    static void check(boolean ok,String message){if(!ok)throw new AssertionError(message);}
    static SharedPreferences memory(Map<String,Object> values) {
        return (SharedPreferences)Proxy.newProxyInstance(MenuSettingsTest.class.getClassLoader(),new Class[]{SharedPreferences.class},(p,m,a)-> {
            switch(m.getName()) {
            case "getInt": case "getBoolean": return values.getOrDefault((String)a[0],a[1]);
            case "contains":return values.containsKey(a[0]);
            case "edit":return Proxy.newProxyInstance(MenuSettingsTest.class.getClassLoader(),new Class[]{SharedPreferences.Editor.class},(e,em,ea)-> {
                if(em.getName().startsWith("put")){values.put((String)ea[0],ea[1]);return e;}
                if(em.getName().equals("commit"))return true;
                return null;
            });
            default:throw new UnsupportedOperationException(m.getName());
            }
        });
    }
    public static void main(String[] args) {
        Map<String,Object> effectsDisk=new HashMap<>();
        RuneSettings effects=new RuneSettings(memory(effectsDisk),0,0,false,false);
        check(effects.characterChoice(false)==0 && effects.characterChoice(true)==-1,"separate roster defaults");
        effects.characterChoice(false,11,1);effects.characterChoice(true,35,0);
        RuneSettings restored=new RuneSettings(memory(effectsDisk),0,0,false,false);
        check(restored.characterChoice(false)==267 && restored.characterChoice(true)==35,"roster persistence");
        restored.characterChoice(true,-1,0);check(restored.characterChoice(false)==267 && restored.characterChoice(true)==-1,"original campaign independent");
        effectsDisk.put("multiplayerCharacter",65535);check(restored.characterChoice(false)==0,"invalid roster fallback");
        check(effects.get("ambientOcclusion")==0,"Ambient occlusion defaults off");
        check(effects.get("snow")==0,"Snow defaults off");
        check(effects.get("networkDiagnostics")==0,"Diagnostics default off");
        check(effects.get("networkSmoothing")==1,"Adaptive smoothing default");
        effects.set("networkSmoothing",0);
        check(new RuneSettings(memory(effectsDisk),0,0,false,false).get("networkSmoothing")==0,"Smoothing persists independently");
        effects.set("networkDiagnostics",1);
        check(new RuneSettings(memory(effectsDisk),0,0,false,false).get("networkDiagnostics")==1,"Diagnostics persist independently");
        effects.reset(new String[]{"networkDiagnostics"});
        effects.set("snow",2);effects.compare();effects.set("snow",1);
        check(!effects.comparing&&effects.get("snow")==1,"Changing snow leaves original comparison");
        effects=new RuneSettings(memory(effectsDisk),0,0,false,false);
        check(effects.get("snow")==1&&effects.label("snow").equals("Light"),"Snow reloads through shared menu model");
        effects.set("snow",99);check(effects.get("snow")==2,"Snow choice clamps");
        effects.reset(new String[]{"snow"});check(effects.get("snow")==0,"Snow reset");
        effects.set("ambientOcclusion",3);
        check((effects.world()&49152)==49152,"High AO survives world feature packing");
        effects=new RuneSettings(memory(effectsDisk),0,(Integer)effectsDisk.get("worldFeatures"),false,false);
        check(effects.get("ambientOcclusion")==3,"AO reloads from saved world features");
        effects.set("ambientOcclusion",0);
        check(effects.get("fireEffects")==1,"Enhanced effects default");
        effects.set("fireEffects",0);effects=new RuneSettings(memory(effectsDisk),0,0,false,false);
        check(effects.get("fireEffects")==0,"Original effects persists");
        effects.compare();effects.set("fireEffects",1);
        check(!effects.comparing,"Changing effects exits original comparison");
        Map<String,Object> bloodDisk=new HashMap<>();
        RuneSettings blood=new RuneSettings(memory(bloodDisk),0,0,false,false);
        check(blood.get("bloodlustMoves")==1,"Bloodlust moves default");
        blood.set("bloodlustMoves",0);blood=new RuneSettings(memory(bloodDisk),0,0,false,false);
        check(blood.get("bloodlustMoves")==0,"Bloodlust toggle persists");
        blood.reset(new String[]{"bloodlustMoves"});check(blood.get("bloodlustMoves")==1,"Bloodlust reset");
        check(blood.get("normalBloodlust")==0,"Normal-combat leap/rush defaults off");
        blood.set("normalBloodlust",1);blood=new RuneSettings(memory(bloodDisk),0,0,false,false);
        check(blood.get("normalBloodlust")==1,"Normal-combat leap/rush persists");
        blood.reset(new String[]{"normalBloodlust"});check(blood.get("normalBloodlust")==0,"Normal-combat rule reset");
        Map<String,Object> tacticsDisk=new HashMap<>();
        RuneSettings tactics=new RuneSettings(memory(tacticsDisk),0,0,false,false);
        check(tactics.get("tactics")==1,"New tactics default");
        tactics.set("difficulty",2);tactics.set("tactics",0);
        tactics=new RuneSettings(memory(tacticsDisk),0,0,false,false);
        check(tactics.get("tactics")==0&&tactics.get("difficulty")==2,"Original tactics must persist without changing difficulty");
        tactics.reset(new String[]{"tactics"});
        check(tactics.get("tactics")==1&&tactics.get("difficulty")==2,"Tactics reset changed difficulty");
        Map<String,Object> textureDisk=new HashMap<>();
        RuneSettings texture=new RuneSettings(memory(textureDisk),0,0,false,false);
        check(texture.get("texturePack")==0,"Texture artwork defaults to Original");
        Map<String,Object> oldTexture=new HashMap<>();oldTexture.put("menu.texturePack",2);
        RuneSettings oldPack=new RuneSettings(memory(oldTexture),0,0,false,false);oldPack.textureWip(false);oldPack.textureWip(true);
        check(oldPack.get("texturePack")==2,"Existing 4x choice must survive the new toggle");
        texture.textureWip(true);check(texture.get("texturePack")==1,"WIP toggle defaults to 2x");
        texture.set("texturePack",2);texture.textureWip(false);
        check(texture.get("texturePack")==0,"Original toggle disables pack");
        texture.textureWip(true);check(texture.get("texturePack")==2,"WIP toggle restores 4x choice");
        check(texture.label("texturePack").contains("WIP"),"WIP artwork must be labelled");
        texture.compare();
        check(texture.comparing&&texture.get("texturePack")==2,"Comparison preserves pack choice");
        texture.set("texturePack",1);
        check(!texture.comparing,"Editing pack exits comparison");
        check(new RuneSettings(memory(textureDisk),0,0,false,false).get("texturePack")==1,"Texture quality persists");
        check(!textureDisk.containsKey("worldFeatures"),"Texture change leaves lighting choices alone");
        Map<String,Object> scalingDisk=new HashMap<>();
        SharedPreferences scalingPrefs=memory(scalingDisk);
        RuneSettings fresh=new RuneSettings(scalingPrefs,0,0,false,false);
        check(fresh.get("resolution")==2&&fresh.get("scalingMethod")==0,"Fresh Android defaults must be Hardware 50%");
        scalingDisk.put("upscale",true);
        RuneSettings scaling=new RuneSettings(scalingPrefs,0,0,false,true);
        check(scaling.get("resolution")==1&&scaling.get("scalingMethod")==1,"Legacy upscaling must remain GSR 75%");
        scaling.set("resolution",2);scaling.set("scalingMethod",0);
        scaling=new RuneSettings(scalingPrefs,0,0,false,true);
        check(scaling.get("resolution")==2&&scaling.get("scalingMethod")==0,"System 50% must survive legacy bool migration and restart");
        scaling.set("music",20);
        check(scalingDisk.get("menu.resolution").equals(2)&&scalingDisk.get("menu.scalingMethod").equals(0),"Audio must preserve both scaling choices");
        scaling.reset(new String[]{"resolution","scalingMethod"});
        check(scaling.get("resolution")==2&&scaling.get("scalingMethod")==0,"Scaling reset must return to Hardware 50%");
        scaling.set("resolution",3);
        check(new RuneSettings(scalingPrefs,0,0,false,true).label("resolution").equals("25%"),"Quarter resolution must survive restart");
        check(scaling.frameTarget()==120&&scaling.refreshTarget()==120,"Timing defaults must retain 120");
        scaling.set("fps",3);check(scaling.frameTarget()==144,"144 FPS mapping");
        scaling.set("fps",4);check(scaling.frameTarget()==165,"165 FPS mapping");
        scaling.customRate("fps",240);scaling.customRate("refresh",360);
        scaling=new RuneSettings(scalingPrefs,0,0,false,false);
        check(scaling.frameTarget()==240&&scaling.refreshTarget()==360,"Custom timing values lost on restart");
        Map<String,Object> timingBefore=new HashMap<>(scalingDisk);
        for(int invalid:new int[]{0,29,1001,Integer.MAX_VALUE}) {
            boolean rejected=false;try{scaling.customRate("refresh",invalid);}catch(IllegalArgumentException e){rejected=true;}
            check(rejected&&scalingDisk.equals(timingBefore),"Invalid custom rate changed preferences");
        }
        scaling.set("refresh",0);check(scaling.refreshTarget()==0&&scaling.frameTarget()==240,"Automatic refresh must not change FPS target");
        scaling.reset(new String[]{"fps","fpsManual","refresh","refreshManual","pacing"});
        check(scaling.frameTarget()==120&&scaling.refreshTarget()==120&&scaling.get("fpsManual")==120,"Timing reset defaults");
        check(scaling.get("fov")==90&&scaling.get("wideView")==1,"Camera defaults");
        scaling.set("fov",999);check(scaling.get("fov")==120&&scaling.label("fov").equals("120°"),"FOV upper bound and degrees");
        scaling.set("fov",0);check(scaling.get("fov")==70,"FOV lower bound");
        scaling.set("fov",105);scaling.set("wideView",0);
        scaling=new RuneSettings(scalingPrefs,0,0,false,false);
        check(scaling.get("fov")==105&&scaling.get("wideView")==0,"Camera restart persistence");
        scaling.reset(new String[]{"fov","wideView"});
        check(scaling.get("fov")==90&&scaling.get("wideView")==1,"Camera reset");
        Map<String,Object> legacy=new HashMap<>();legacy.put("worldFeatures",7);
        RuneSettings legacySettings=new RuneSettings(memory(legacy),0,0,false,false);
        legacySettings.set("pixelLighting",1);
        check(legacy.get("worldFeatures").equals(263),"Lighting must preserve legacy bits despite diagnostic override");
        legacySettings.set("pixelLighting",0);
        check(legacy.get("worldFeatures").equals(7),"Lighting off must restore exact legacy world value");
        Map<String,Object> disk=new HashMap<>();disk.put("upscale",true);disk.put("enhanced",true);
        SharedPreferences prefs=memory(disk);
        RuneSettings s=new RuneSettings(prefs,13,7,true,false);
        check(s.get("detail")==2&&s.get("depth")==2,"Legacy On must retain Medium strength");
        s.set("music",42);
        check(disk.get("upscale").equals(true),"Volume must not persist a diagnostic native-resolution override");
        for(int d=0;d<4;d++)for(int p=0;p<4;p++){
            s.set("detail",d);s.set("depth",p);
            int world=s.world();
            check(((world&2)!=0)==(d>0)&&((world&4)!=0)==(p>0),"Enable bits disagree with levels");
            check(((world>>4)&3)==d&&((world>>6)&3)==p,"Quality fields overlap");
        }
        int oldWorld=s.world();s.set("pixelLighting",1);
        check(s.world()==(oldWorld|256),"Per-pixel lighting must not change existing feature bits");
        s.compare();s.set("pixelLighting",0);check(!s.comparing&&s.world()==oldWorld,"Lighting off / comparison exit");
        s.set("pixelLighting",2);
        check(s.world()==(oldWorld|768),"Remastered must enable pixel lighting and shadows without changing other bits");
        for(String key:new String[]{"materialMaps","water","shadowLights"})s.set(key,1);
        check((s.world()&7168)==7168,"New feature bits must not overlap");
        int newWorld=s.world();s.set("water",0);check(s.world()==(newWorld&~2048),"Water toggle changed another feature");s.set("water",1);
        s.set("water",2);
        check((s.world()&10240)==10240,"Full scene water must retain its enable bit");
        RuneSettings fullReloaded=new RuneSettings(prefs,0,(Integer)disk.get("worldFeatures"),false,false);
        check(fullReloaded.get("water")==2,"Full scene water lost on restart");
        s.set("water",1);check((s.world()&10240)==2048,"Approximate must disable full scene only");
        s.set("water",0);check((s.world()&10240)==0,"Original must clear both water bits");
        s.set("water",1);
        s.set("sharp",3);s.set("shadow",3);s.set("filter",1);
        check(s.clarity()==31,"High must survive clarity packing");
        Map<String,Object> before=new HashMap<>(disk);int resolution=s.get("resolution"),fps=s.get("fps");
        s.compare();check(s.comparing,"Comparison did not enable");
        check(s.get("resolution")==resolution&&s.get("fps")==fps&&disk.equals(before),"Comparison changed saved values or performance settings");
        s.compare();check(!s.comparing&&disk.equals(before),"Comparison did not restore without writing preferences");
        s.compare();s.set("depth",2);check(!s.comparing,"Editing an effect should leave original comparison");
        RuneSettings reloaded=new RuneSettings(prefs,(Integer)disk.get("claritySettings"),(Integer)disk.get("worldFeatures"),true,(Boolean)disk.get("upscale"));
        check(reloaded.get("sharp")==3&&reloaded.get("shadow")==3&&reloaded.get("detail")==3&&reloaded.get("depth")==2,"Quality selections did not survive restart");
        check(reloaded.get("pixelLighting")==2,"Lighting selection lost on restart");
        check((reloaded.world()&7168)==7168,"New effects lost on restart");
        check(reloaded.get("music")==42,"Volume did not survive restart");
        System.out.println("PASS: migration, quality packing, original comparison, preference isolation and persistence");
    }
}
