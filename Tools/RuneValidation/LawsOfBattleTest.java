package org.runeclassic.port;
import java.util.*;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;

public final class LawsOfBattleTest {
 static void check(boolean b,String why){if(!b)throw new AssertionError(why);}
 public static void main(String[] args)throws Exception{
  Map<String,Object> disk=new HashMap<>();RuneSettings s=new RuneSettings(MenuSettingsTest.memory(disk),0,0,false,false);
  s.set("kick",0);s.set("dualWield",0);s.set("bloodlustMoves",0);
  disk.put("laws.host",4095);disk.put("unrelated",123);
  int migrated=RuneBattleLaws.campaign(s);
  check((migrated&(1|4|16|32))==0,"Preserve campaign opt-outs, always remove leap");
  check((migrated&2)!=0,"Unrelated laws retain their defaults");
  for(int mask=0;mask<=RuneBattleLaws.ALL;mask++){
   RuneBattleLaws.campaign(s,mask);RuneSettings reload=new RuneSettings(MenuSettingsTest.memory(disk),0,0,false,false);
   check(RuneBattleLaws.campaign(reload)==(mask&~16),"Campaign choices persist without leap");
   check((Integer)disk.get("laws.host")==4095,"Campaign never overwrites host choices");
   check((RuneBattleLaws.effective(mask)&8)==0||(mask&4)!=0,"Crossed block needs dual wield");
  }
  check((Integer)disk.get("unrelated")==123,"Other settings preserved");
  Path root=Files.createTempDirectory("rune-laws-");
  try{
   Files.createDirectories(root.resolve("Maps"));Files.createDirectories(root.resolve("System"));Files.createFile(root.resolve("Maps/DM-Hildir.run"));Files.createFile(root.resolve("System/RuneI.u"));
   RuneHostConfig c=new RuneHostConfig();c.normalize(root.toFile());
   for(int mask:new int[]{0,1,4095,4095&~2048}){
    c.laws=mask;List<String> out=Arrays.asList(c.arguments(root.toFile(),root.resolve("session").toFile(),"127.0.0.1"));
    check(out.contains("--rune-battle-laws="+mask),"Exact host laws reach server");
    check(out.contains("--rune-normal-bloodlust-moves="+((mask&2048)!=0?1:0)),"Legacy normal flag agrees with selected laws");
   }
   c.laws=4096;try{c.validate(root.toFile());throw new AssertionError("Invalid mask accepted");}catch(IllegalArgumentException expected){}
   String invite="OPEN-RUNE-INVITE-5\n127.0.0.1 1234\nDM-Hildir\n1 1\n"+"a".repeat(32)+"\n"+"b".repeat(64)+"\n2\n1\n2 1 0\n";
   for(int mask:new int[]{0,4095})check(new String(RuneInvitation.normalize((invite+mask+"\n").getBytes(StandardCharsets.UTF_8)),StandardCharsets.UTF_8).endsWith(mask+"\n"),"Invite retains host rules");
   for(String bad:new String[]{"-1","4096","4294967296","0 extra"})try{RuneInvitation.normalize((invite+bad+"\n").getBytes(StandardCharsets.UTF_8));throw new AssertionError("Invalid invitation mask accepted");}catch(IOException expected){}
  }finally{try(var paths=Files.walk(root)){paths.sorted(Comparator.reverseOrder()).forEach(p->{try{Files.delete(p);}catch(IOException e){throw new RuntimeException(e);}});}}
  check(RuneBattleLaws.NAMES.length==12&&RuneBattleLaws.HELP.length==12,"Every law has a description");
  System.out.println("LAWS_ANDROID_MODEL_OK 4096 preference combinations, migration, host isolation, argument and invitation validation");
 }
}
