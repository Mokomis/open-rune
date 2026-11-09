package org.runeclassic.port;
import java.util.*;

/** Reject lost or duplicate settings when the menu hierarchy is reorganized. */
public final class MenuNavigationTest {
    public static void main(String[] args){
        Map<String,Object> disk=new HashMap<>();
        RuneSettings model=new RuneSettings(MenuSettingsTest.memory(disk),0,0,false,false);
        Set<String> seen=new HashSet<>();
        for(String category:new String[]{"Video","Audio","Controls","Interface","Gameplay","Support","Connection"})
            for(String page:RuneMenuSections.sections(category))
                for(String key:RuneMenuSections.keys(page)){
                    if(!model.items.containsKey(key))throw new AssertionError("Unknown setting: "+key);
                    if(!seen.add(key))throw new AssertionError("Duplicate parent for: "+key);
                }
        // Superseded controls remain only as migration inputs; Laws of Battle owns these choices.
        seen.addAll(Arrays.asList("kick","bash","dualWield","bloodlustMoves","normalBloodlust","tactics"));
        if(!seen.equals(model.items.keySet())){Set<String> missing=new HashSet<>(model.items.keySet());missing.removeAll(seen);throw new AssertionError("Unreachable: "+missing);}
        model.set("bloom",1);model.set("master",42);model.set("fps",4);model.customRate("refresh",144);
        model.reset(RuneMenuSections.keys("camera"));
        if(model.get("bloom")!=1||model.get("master")!=42||model.get("fps")!=4||model.refreshTarget()!=144)throw new AssertionError("Section reset changed unrelated preferences");
        System.out.println("PASS: "+seen.size()+" settings each have one parent; section reset preserves other sections");
    }
}
