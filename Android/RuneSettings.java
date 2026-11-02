package org.runeclassic.port;

import android.content.SharedPreferences;
import java.util.LinkedHashMap;

/** One model for every occurrence of a setting, including Enhancements. */
final class RuneSettings {
    interface Listener { void changed(String key); }
    static final String[] LEVELS={"Off","Low","Medium","High"};
    static final String[] SWITCH={"Off","On"};
    static final class Item {
        final String key, name, hint; final String[] choices; final int defaultValue;
        int value;
        Item(String k,String n,String h,String[] c,int d) { key=k;name=n;hint=h;choices=c;defaultValue=d;value=d; }
        String label() { return key.equals("fov") ? value+"°" : choices==null ? value+"%" : choices[value]; }
    }
    final LinkedHashMap<String,Item> items=new LinkedHashMap<>();
    final SharedPreferences prefs;
    Listener listener;
    boolean comparing;
    RuneSettings(SharedPreferences p,int clarity,int world,boolean enhanced,boolean upscale) {
        prefs=p;
        add("resolution","Rendering resolution","Percentage of display width and height. Lower values reduce GPU work.",new String[]{"Native (100%)","75%","50%","25%"},2);
        add("scalingMethod","Scaling method","Hardware Scaling uses dedicated display hardware when available. Snapdragon GSR uses a software filter on the GPU. Android may fall back to GPU scaling. Native bypasses both.",new String[]{"Hardware Scaling","Snapdragon GSR"},0);
        add("aa","Anti-aliasing","Smooths geometry edges. Higher MSAA levels require more GPU work.",new String[]{"Off","2× MSAA","4× MSAA"},2);
        add("fps","Frame rate target","Controls how many frames the game tries to produce each second. Separate from display refresh.",new String[]{"30 FPS","60 FPS","120 FPS","144 FPS","165 FPS","Custom…"},2);
        add("fpsManual","Custom FPS","",null,120);
        add("refresh","Refresh rate","Requests a supported display mode. Android may choose another rate.",new String[]{"Automatic","60 Hz","90 Hz","120 Hz","144 Hz","165 Hz","Custom…"},3);
        add("refreshManual","Custom refresh","",null,120);
        add("pacing","Frame pacing","Synchronizes presentation with the display.",SWITCH,1);
        add("fov","Field of view","Base horizontal view at 4:3. Wider screens expand the sides automatically.",null,90);
        add("wideView","Widescreen framing","Automatic preserves vertical visibility as the screen gets wider.",new String[]{"Original","Automatic"},1);
        add("bloom","Bloom & color","Subtle highlight glow and warmer color tuning.",new String[]{"Original","Enhanced"},0);
        add("fireEffects","Fire & combat effects","Fuller flames, rising embers and motion-aligned sparks, debris and blood droplets. Cosmetic only, with a shared particle budget.",new String[]{"Original","Enhanced"},1);
        add("snow","Snow","Off keeps original map weather. Light and Heavy add drifting snow beneath open sky, with roof shelter. Cosmetic only.",new String[]{"Off","Light","Heavy"},0);
        add("sharp","Adaptive sharpening","Restores fine contrast while limiting bright edge halos.",LEVELS,1);
        add("filter","Texture filtering","Keeps angled textures clear; limited by GPU support.",new String[]{"8×","16×"},1);
        add("shadow","Shadow / midtone lift","Reveals dark detail while preserving deep blacks.",LEVELS,1);
        add("pixelLighting","Lighting mode","Remastered adds nearby moving shadows, stronger light and material highlights.",new String[]{"Original","Per-pixel","Remastered"},0);
        add("materialMaps","Material maps","Generated normal and roughness detail for Remastered lighting.",new String[]{"Original","Enhanced"},0);
        add("texturePack","Texture artwork","Work-in-progress remastered artwork. Unfinished or unavailable textures keep their original artwork.",new String[]{"Original","WIP 2×","WIP 4×"},0);
        add("water","Water reflections","Approximate adds ripples and an environment sheen. Full scene reflects nearby scenery at higher cost.",new String[]{"Original","Approximate","Full scene"},0);
        add("shadowLights","Shadow-casting lights","Remastered lighting: one or three nearby lights cast moving shadows.",new String[]{"One nearby","Three nearby"},0);
        add("ambientOcclusion","Ambient occlusion","Experimental contact shading around corners and objects. Off is recommended at native resolution until this effect is faster.",LEVELS,0);
        add("detail","Surface detail","Generated relief on stone and wood. High strengthens shading.",LEVELS,0);
        add("depth","Surface depth","Parallax on stone and wood. High uses 16 steps and costs more GPU time.",LEVELS,0);
        add("environment","Environmental sound","Room reverb and sound muffling through walls.",SWITCH,1);
        add("hrtf","Headphone spatial audio","Directional binaural audio; best with stereo headphones.",SWITCH,0);
        add("master","Master volume","Overall game volume.",null,100);
        add("music","Music volume","Music and ambient score.",null,100);
        add("effects","Effects volume","Combat, voices and environmental sounds.",null,100);
        add("sensitivity","Look sensitivity","Touch and controller camera sensitivity.",new String[]{"Very low","Low","Normal","High","Very high"},2);
        add("invert","Invert vertical look","Reverse vertical touch and controller look.",SWITCH,0);
        add("touch","Touch controls","Shown when no gamepad is connected. Menu always remains available.",SWITCH,1);
        add("rumble","Rumble","Impact feedback on supported controllers or devices.",SWITCH,1);
        add("rumbleStrength","Rumble intensity","Without strength control, weaker feedback uses shorter pulses.",null,65);
        add("rumbleOutput","Vibration output","Automatic prefers the active controller, then the device vibrator.",new String[]{"Automatic","Controller","Device"},0);
        add("opacity","Control opacity","Adjust the visibility of the on-screen controls.",new String[]{"35%","50%","65%","80%","100%"},2);
        add("counter","Show FPS","Displays engine FPS; actual display timing is measured separately.",SWITCH,1);
        add("networkDiagnostics","Network diagnostics","Multiplayer frame times, acknowledged round-trip time, snapshot gaps and transport timeouts. Timeouts are not a packet-loss measurement.",SWITCH,0);
        add("networkSmoothing","Network smoothing","Adaptive smooths remote movement against packet jitter with a bounded buffer. Local controls stay responsive; gameplay remains server controlled.",new String[]{"Original","Adaptive"},1);
        add("hud","HUD style","Themed meters with independently saved positions and sizes.",new String[]{"Original","Open Rune"},1);
        add("kick","Combat kicks","F / right-stick click. Raised shields absorb damage and most knockback.",SWITCH,1);
        add("bash","Shield bash","Double-tap Block. Brief stagger and small shove; raised shields counter it.",SWITCH,1);
        add("normalBloodlust","Leap & rush without bloodlust","Allow giant leap and shield rush outside bloodlust. Outside bloodlust no meter is spent; the shared 4-second cooldown and shield requirement remain. Host setting applies to new matches.",SWITCH,0);
        add("bloodlustMoves","Bloodlust moves","While bloodlust is active: double-tap Jump for a giant leap; hold Block and press Attack for shield charge. Each costs half a meter, with a 4-second cooldown. Amber controls show readiness. Selected Dark Vikings can use them too. Online matches use the host rules.",new String[]{"Off","On"},1);
        add("dualWield","Dual wield","Use two light weapons and a weaker crossed block. Your shield stays in inventory; turn this off to use it again. Compatible Vikings can dual wield too.",new String[]{"Off","On"},1);
        add("tactics","Enemy tactics","Tactical humanoids use spacing, delayed defense, kicks and shield bashes. Bosses and scripted scenes keep their original behavior.",new String[]{"Original","Tactical"},1);
        add("difficulty","New campaign difficulty","Applies when you start a new campaign. Saves retain their difficulty.",new String[]{"Easy","Medium","Hard","Very hard"},1);
        // Existing installs retain their choices; old enabled relief equals Medium.
        if(!prefs.contains("menu.resolution") && prefs.contains("upscale")) {
            items.get("resolution").value=upscale?1:0;
            if(!prefs.contains("menu.scalingMethod"))items.get("scalingMethod").value=1;
        }
        items.get("bloom").value=enhanced?1:0;
        items.get("sharp").value=clarity&3;
        items.get("filter").value=(clarity&4)!=0?1:0;
        items.get("shadow").value=(clarity>>3)&3;
        items.get("pixelLighting").value=(world&512)!=0?2:(world>>8)&1;
        items.get("materialMaps").value=(world>>10)&1;
        items.get("water").value=(world&2048)==0?0:(world&8192)!=0?2:1;
        items.get("shadowLights").value=(world>>12)&1;
        items.get("ambientOcclusion").value=(world>>14)&3;
        items.get("environment").value=world&1;
        items.get("hrtf").value=(world>>3)&1;
        items.get("detail").value=(world&2)==0?0:Math.max(1,((world>>4)&3)==0?2:(world>>4)&3);
        items.get("depth").value=(world&4)==0?0:Math.max(1,((world>>6)&3)==0?2:(world>>6)&3);
    }
    private void add(String key,String name,String hint,String[] choices,int value) {
        Item item=new Item(key,name,hint,choices,value);
        item.value=bounded(item,prefs.getInt("menu."+key,value));
        items.put(key,item);
    }
    private static int bounded(Item item,int value) {
        if(item.key.equals("fov"))return Math.max(70,Math.min(120,value));
        if(item.key.equals("fpsManual")||item.key.equals("refreshManual"))return Math.max(30,Math.min(1000,value));
        return Math.max(0,Math.min(item.choices==null?100:item.choices.length-1,value));
    }
    int frameTarget(){return get("fps")==5?get("fpsManual"):new int[]{30,60,120,144,165}[get("fps")];}
    int refreshTarget(){return get("refresh")==6?get("refreshManual"):new int[]{0,60,90,120,144,165}[get("refresh")];}
    String label(String key) {
        if(key.equals("fps")&&get(key)==5)return "Custom "+frameTarget()+" FPS";
        if(key.equals("refresh")&&get(key)==6)return "Custom "+refreshTarget()+" Hz";
        return items.get(key).label();
    }
    void customRate(String key,int value) {
        if((!key.equals("fps")&&!key.equals("refresh"))||value<30||value>1000)throw new IllegalArgumentException("Enter a whole number from 30 to 1000");
        items.get(key+"Manual").value=value;
        items.get(key).value=items.get(key).choices.length-1;
        prefs.edit().putInt("menu."+key+"Manual",value).putInt("menu."+key,items.get(key).value).apply();
        if(listener!=null)listener.changed(key);
    }
    void textureWip(boolean enabled) {
        if(get("texturePack")>0)prefs.edit().putInt("menu.textureWipQuality",get("texturePack")).apply();
        int quality=Math.max(1,Math.min(2,prefs.getInt("menu.textureWipQuality",1)));
        set("texturePack",enabled?quality:0);
    }
    int characterChoice(boolean campaign) {
        int value=prefs.getInt(campaign?"campaignCharacter":"multiplayerCharacter",campaign?-1:0);
        int character=value&255,skin=(value>>8)&255;
        return value>=0&&value<=65535&&character<36&&skin<(character==11?2:1)?value:campaign?-1:0;
    }
    void characterChoice(boolean campaign,int character,int skin) {
        int value=character<0?-1:character|(skin<<8);
        prefs.edit().putInt(campaign?"campaignCharacter":"multiplayerCharacter",value).apply();
        if(listener!=null)listener.changed("character");
    }
    int get(String key) { return items.get(key).value; }
    void set(String key,int value) {
        Item item=items.get(key);
        item.value=bounded(item,value);
        prefs.edit().putInt("menu."+key,item.value).apply();
        // Persist only the changed family: changing volume must not save a
        // diagnostic launch's resolution/effect override as the user's default.
        SharedPreferences.Editor legacy=prefs.edit();
        if(key.equals("bloom"))legacy.putBoolean("enhanced",get("bloom")!=0);
        if(key.equals("resolution"))legacy.putBoolean("upscale",get("resolution")!=0);
        if(key.equals("sharp")||key.equals("filter")||key.equals("shadow"))
            legacy.putBoolean("clarityEnabled",true).putInt("claritySettings",clarity());
        // Changing only mesh lighting preserves legacy quality encodings and
        // unrelated saved world effects, including during diagnostic overrides.
        if(key.equals("pixelLighting"))
            legacy.putInt("worldFeatures",(prefs.getInt("worldFeatures",world())&~768)|lightingBits());
        if(key.equals("detail")||key.equals("depth")||key.equals("environment")||key.equals("hrtf"))
            legacy.putInt("worldFeatures",world());
        int featureBit=key.equals("materialMaps")?1024:key.equals("shadowLights")?4096:0;
        if(featureBit!=0) {legacy.putInt("worldFeatures",(prefs.getInt("worldFeatures",world())&~featureBit)|(item.value!=0?featureBit:0));comparing=false;}
        if(key.equals("ambientOcclusion")) {legacy.putInt("worldFeatures",(prefs.getInt("worldFeatures",world())&~49152)|(item.value<<14));comparing=false;}
        if(key.equals("water")) {legacy.putInt("worldFeatures",(prefs.getInt("worldFeatures",world())&~10240)|waterBits());comparing=false;}
        legacy.apply();
        if(key.equals("texturePack")) {
            comparing=false;
            if(item.value>0)prefs.edit().putInt("menu.textureWipQuality",item.value).apply();
        }
        if(key.equals("fireEffects")||key.equals("snow"))comparing=false;
        if(key.equals("bloom")||key.equals("sharp")||key.equals("filter")||key.equals("shadow")||key.equals("pixelLighting")||key.equals("detail")||key.equals("depth")||key.equals("environment")||key.equals("hrtf"))comparing=false;
        if(listener!=null) listener.changed(key);
    }
    int clarity() { return get("sharp")|(get("filter")<<2)|(get("shadow")<<3); }
    int lightingBits() { return get("pixelLighting")==2?768:get("pixelLighting")<<8; }
    int waterBits() { return get("water")==0?0:2048|(get("water")==2?8192:0); }
    int world() { return get("environment")|(get("hrtf")<<3)|(get("detail")>0?2:0)|(get("depth")>0?4:0)|(get("detail")<<4)|(get("depth")<<6)|lightingBits()|(get("materialMaps")<<10)|waterBits()|(get("shadowLights")<<12)|(get("ambientOcclusion")<<14); }
    void compare() { comparing=!comparing; if(listener!=null) listener.changed("compare"); }
    void reset(String[] keys) {
        for(String key:keys) { Item i=items.get(key); set(key,i.defaultValue); }
    }
}
