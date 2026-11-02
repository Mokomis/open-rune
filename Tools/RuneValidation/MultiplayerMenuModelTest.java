package org.runeclassic.port;

import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Menu model/import checks only. Does not repeat the engine's map playtests. */
public final class MultiplayerMenuModelTest {
    static void check(boolean condition,String message){if(!condition)throw new AssertionError(message);}
    static void rejects(Runnable action){try{action.run();throw new AssertionError("Accepted invalid host choices");}catch(IllegalArgumentException expected){}}
    static void badInvite(String text){try{RuneInvitation.normalize(text.getBytes(StandardCharsets.UTF_8));throw new AssertionError("Accepted invalid invitation");}catch(IOException expected){}}
    static RuneHostConfig config(String mode,String variant){
        RuneHostConfig c=new RuneHostConfig();c.mode=mode;
        if(mode.equals("tdm")||mode.equals("headball"))c.teams=variant.charAt(0)-'0';
        if(mode.equals("arena"))c.arenaSize=variant.charAt(0)-'0';
        c.limbs=variant.endsWith("limbs on");c.players=Math.max(4,c.minimumPlayers());return c;
    }
    public static void main(String[] args)throws Exception{
        List<String> lines=Files.readAllLines(Paths.get(args[0]),StandardCharsets.UTF_8);
        Set<String> maps=new TreeSet<>(),variants=new TreeSet<>(),expected=new HashSet<>();
        for(String line:lines.subList(1,lines.size())){
            String[] row=line.split(",");maps.add(row[2]);variants.add(row[0]+":"+row[1]);expected.add(row[0]+":"+row[1]+":"+row[2]);
        }
        Path root=Files.createTempDirectory("open-rune-host-model-");
        try{
            File game=root.resolve("game").toFile();Files.createDirectories(root.resolve("game/Maps"));Files.createDirectories(root.resolve("game/System"));Files.createFile(root.resolve("game/System/RuneI.u"));
            for(String map:maps)Files.createFile(root.resolve("game/Maps/"+map+".run"));
            int accepted=0,filtered=0;
            for(String variant:variants){
                String[] parts=variant.split(":");RuneHostConfig c=config(parts[0],parts[1]);
                for(String map:maps){
                    boolean supported=expected.contains(variant+":"+map);
                    check(RuneHostConfig.supports(map,c.mode,c.teams,c.arenaSize)==supported,"Wrong map filter: "+variant+":"+map);
                    c.maps.clear();c.maps.add(map);
                    if(supported){c.validate(game);List<String> launch=Arrays.asList(c.arguments(game,root.resolve("session").toFile(),"192.168.1.2"));check(launch.contains("--rune-net-maps="+map)&&launch.contains("--rune-net-mode="+c.mode),"Map/mode not passed to engine");accepted++;}
                    else{rejects(()->c.validate(game));filtered++;}
                }
            }
            RuneHostConfig c=new RuneHostConfig();c.normalize(game);check(c.maps.equals(Arrays.asList("DM-Hildir")),"Default map");
            c.maps.add("DM-Hildir");rejects(()->c.validate(game));c.maps.remove(1);
            c.mode="arena";c.arenaSize=4;c.normalize(game);check(c.players==8&&c.maps.equals(Arrays.asList("AR-Champions")),"Arena normalizes roster and map");
            c.players=4;rejects(()->c.validate(game));c.players=8;c.score=0;c.minutes=0;rejects(()->c.validate(game));c.minutes=10;
            c.maps.clear();c.maps.add("../System/RuneI");rejects(()->c.validate(game));
            String base="192.168.1.2 43210\nDM-Hildir\n2 1\n0123456789abcdef0123456789abcdef\n0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";
            for(int v=1;v<=4;v++){
                String text="OPEN-RUNE-INVITE-"+v+"\n"+base+(v>=2?"4\n":"")+(v>=3?"1\n":"")+(v==4?"2 1 0\n":"");
                check(Arrays.equals(RuneInvitation.normalize(text.getBytes(StandardCharsets.UTF_8)),RuneInvitation.normalize(text.replace("\n","\r\n").getBytes(StandardCharsets.UTF_8))),"CRLF normalization");
                badInvite(text+"extra");badInvite(text.replace("43210","65536"));badInvite(text.replace("192.168.1.2","999.1.1.1"));badInvite(text.replace("DM-Hildir","../file"));
            }
            badInvite("OPEN-RUNE-INVITE-4\n");badInvite(new String(new char[1025]).replace('\0','x'));
            check(accepted==157&&maps.size()==44&&variants.size()==14,"Unexpected evidence matrix");
            System.out.println("PASS: 157 supported menu combinations, "+filtered+" unsupported combinations filtered; roster/limits/rotation validation; invitation formats 1–4 and malformed imports.");
        }finally{try(java.util.stream.Stream<Path> files=Files.walk(root)){files.sorted(Comparator.reverseOrder()).forEach(p->{try{Files.delete(p);}catch(IOException e){throw new RuntimeException(e);}});}}
    }
}
