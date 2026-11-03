package org.runeclassic.port;
import java.io.*;
import java.nio.file.*;
public final class FirstRunFilesTest {
    static void check(boolean v,String m){if(!v)throw new AssertionError(m);}
    public static void main(String[] args)throws Exception {
        File source=new File(args[0]),copied=new File(args[1]);RuneGameFiles.validate(source);RuneGameFiles.validate(copied);
        check(RuneGameFiles.ready(copied),"Imported files not recognized");
        check(!RuneGameFiles.allowed("System","User.ini")&&!RuneGameFiles.allowed("System","Engine.dll")&&!RuneGameFiles.allowed("Maps","Save1.usa"),"Private files allowed");
        check(RuneGameFiles.allowed("System","Rune.exe")&&RuneGameFiles.allowed("Textures","STONE.UTX"),"Valid files rejected");
        for(String name:new String[]{"../file","..","a/b","a\\b",""}){boolean rejected=false;try{RuneGameFiles.canonical(name);}catch(IOException e){rejected=true;}check(rejected,"Invalid file name allowed");}
        Path root=Files.createTempDirectory("open-rune-java-setup-");
        try {
            check(!RuneGameFiles.ready(root.toFile()),"Empty folder accepted");
            Files.createDirectories(root.resolve("System"));Files.copy(new File(copied,"System/Core.u").toPath(),root.resolve("System/Core.u"));
            check(!RuneGameFiles.ready(root.toFile()),"Incomplete folder accepted");
            // Preparing the actual imported copy must preserve validation and use local paths.
            RuneGameFiles.prepare(copied);check(RuneGameFiles.ready(copied),"Prepared copy invalid");
            String config=new String(Files.readAllBytes(new File(copied,"System/Default.ini").toPath()),"ISO-8859-1");
            check(config.contains("SavePath=..\\Save")&&config.contains("Paths=..\\System\\*.u"),"Config escaped game root");
            System.out.println("PASS Android setup: real data/version, incomplete folders, safe names, import filters, local configuration");
        }finally{RuneGameFiles.removeStage(root.toFile());}
    }
}
