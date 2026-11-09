package org.runeclassic.port;

import java.io.File;
import java.util.*;

/** Host choices shared by the menu and service. No campaign config is written. */
final class RuneHostConfig {
    static final String[] MODES={"dm","tdm","arena","headball"};
    static final String[] NAMES={"Deathmatch","Team Deathmatch","Arena","HeadBall"};
    String mode="dm",matchName="Open Rune match";
    int laws=4095;
    int physics=0,bots=0,players=4,teams=2,arenaSize=1,score=20,minutes=10;
    boolean limbs,discoverable,normalBloodlust=true;
    final ArrayList<String> maps=new ArrayList<>();

    String name(){return NAMES[Arrays.asList(MODES).indexOf(mode)];}
    int minimumPlayers(){return mode.equals("arena")?2*arenaSize:(mode.equals("dm")?2:teams);}
    static boolean supports(String map,String mode,int teams,int arenaSize){
        if(!map.matches("[A-Za-z0-9_-]{1,48}"))return false;
        if(mode.equals("dm"))return map.startsWith("DM-");
        if(mode.equals("tdm")) {
            if(!map.startsWith("DM-"))return false;
            int max=map.equalsIgnoreCase("DM-Team_chasm")||map.equalsIgnoreCase("DM-Team_ships")?2:map.equalsIgnoreCase("DM-TEAM_MadKing")?3:4;
            return teams<=max;
        }
        if(mode.equals("headball"))return map.startsWith("HB-")&&(teams==2||Arrays.asList("HB-Aqueduct","HB-DeCapital","HB-Montezuma","HB-Montezuma2").contains(map));
        if(!mode.equals("arena")||!map.startsWith("AR-"))return false;
        int max=Arrays.asList("AR-Champions","AR-SmeltingPot").contains(map)?4:
            Arrays.asList("AR-AEternal","AR-BladePit").contains(map)?3:
            Arrays.asList("AR-Cathedral","AR-TeamVent","AR-Widowmaker").contains(map)?2:1;
        return arenaSize<=max;
    }
    ArrayList<String> available(File game){
        ArrayList<String> out=new ArrayList<>();File[] files=new File(game,"Maps").listFiles();
        if(files!=null)for(File f:files)if(f.isFile()&&f.getName().endsWith(".run")){
            String name=f.getName().substring(0,f.getName().length()-4);
            if(supports(name,mode,teams,arenaSize))out.add(name);
        }
        Collections.sort(out,String.CASE_INSENSITIVE_ORDER);return out;
    }
    void normalize(File game){
        ArrayList<String> available=available(game);maps.retainAll(available);
        if(maps.isEmpty()&&!available.isEmpty()){
            String preferred=mode.equals("arena")?"AR-Champions":mode.equals("headball")?"HB-Agora":mode.equals("tdm")?"DM-Team_ships":"DM-Hildir";
            maps.add(available.contains(preferred)?preferred:available.get(0));
        }
        players=Math.max(players,minimumPlayers());bots=Math.max(0,Math.min(bots,players-1));
    }
    void validate(File game){
        if(!matchName.matches("[ -~]{1,48}"))throw new IllegalArgumentException("Use a match name of 1–48 letters, numbers or symbols.");
        if(!Arrays.asList(MODES).contains(mode)||teams<2||teams>4||arenaSize<1||arenaSize>4||players<minimumPlayers()||players>16)
            throw new IllegalArgumentException("Choose enough player slots for this mode.");
        if(laws<0||laws>4095)throw new IllegalArgumentException("Invalid Laws of Battle.");
        if(physics<0||physics>2)throw new IllegalArgumentException("Invalid physics mode.");
        if(bots<0||bots>=players)throw new IllegalArgumentException("Leave a player slot for the host.");
        if(score<0||score>10000||minutes<0||minutes>1440||(score==0&&minutes==0))throw new IllegalArgumentException("Enable a score or time limit.");
        if(maps.isEmpty()||maps.size()>32||new HashSet<>(maps).size()!=maps.size()||!available(game).containsAll(maps))
            throw new IllegalArgumentException("Choose installed maps supported by this mode.");
        if(!new File(game,"System/RuneI.u").isFile())throw new IllegalArgumentException("Rune game files are missing.");
    }
    String[] arguments(File game,File session,String address){
        validate(game);
        ArrayList<String> out=new ArrayList<>(Arrays.asList("--direct","--save-directory="+new File(session,"saves"),
            "--rune-server="+new File(session,"invitations"),"--rune-net-address="+address,"--rune-net-mode="+mode,
            "--rune-net-players="+players,"--rune-net-teams="+teams,"--rune-net-arena-team-size="+arenaSize,
            "--rune-net-frag-limit="+score,"--rune-net-round-limit="+score,"--rune-net-time-limit="+(minutes*60),
            "--rune-net-intermission=8","--rune-net-maps="+String.join(",",maps)));
        if(mode.equals("headball")&&limbs)out.add("--rune-net-limbs-score");
        out.add("--rune-net-discovery"); // Also supplies local occupancy to the bot manager.
        out.add("--rune-normal-bloodlust-moves="+((laws&2048)!=0?"1":"0"));
        out.add("--rune-battle-laws="+laws);
        out.add("--rune-physics="+physics);
        out.add(game.getAbsolutePath());return out.toArray(new String[0]);
    }
}
