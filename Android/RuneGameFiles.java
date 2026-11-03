package org.runeclassic.port;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.*;

/** Pure file validation shared by the setup screen and laptop fixtures. */
final class RuneGameFiles {
    static final String STEAM="https://store.steampowered.com/app/210950/Rune_Classic/";
    static final String[] FOLDERS={"System","Maps","Textures","Meshes","Sounds","Music"};
    static String lower(String s){return s.toLowerCase(Locale.ROOT);}
    static File child(File parent,String name){
        File[] files=parent.listFiles();if(files!=null)for(File file:files)if(file.getName().equalsIgnoreCase(name))return file;
        return new File(parent,name);
    }
    static boolean allowed(String folder,String name){
        String n=lower(name),ext=n.lastIndexOf('.')<0?"":n.substring(n.lastIndexOf('.'));
        if(folder.equals("System"))return n.equals("rune.exe")||n.equals("default.ini")||n.equals("defuser.ini")||Arrays.asList(".u",".int",".det",".frt",".est",".itt",".rut").contains(ext);
        if(folder.equals("Maps"))return ext.equals(".run");
        if(folder.equals("Textures"))return ext.equals(".utx");
        if(folder.equals("Meshes"))return ext.equals(".ums");
        if(folder.equals("Sounds"))return ext.equals(".uax");
        return folder.equals("Music")&&(ext.equals(".umx")||ext.equals(".ogg"));
    }
    static String canonical(String name)throws IOException {
        if(name.isEmpty()||name.equals(".")||name.equals("..")||name.indexOf('/')>=0||name.indexOf('\\')>=0||name.indexOf('\0')>=0)throw new IOException("Invalid file name in the selected folder.");
        for(String fixed:new String[]{"Rune.exe","Core.u","Engine.u","RuneI.u","RMenu.u","Default.ini","DefUser.ini"})if(name.equalsIgnoreCase(fixed))return fixed;
        return name;
    }
    static boolean pkg(File f)throws IOException {
        if(f.length()<=32)return false;
        try(InputStream in=new FileInputStream(f)){return in.read()==0xc1&&in.read()==0x83&&in.read()==0x2a&&in.read()==0x9e;}
    }
    static void validate(File root)throws IOException {
        File system=child(root,"System");
        for(String name:new String[]{"Core.u","Engine.u","RuneI.u","RMenu.u"})if(!pkg(child(system,name)))throw new IOException("Missing or invalid System/"+name+". Select the complete Rune Classic folder.");
        for(String name:new String[]{"Default.ini","DefUser.ini","Rune.exe"})if(child(system,name).length()==0)throw new IOException("Missing System/"+name+".");
        if(!pkg(child(child(root,"Maps"),"DM-Hildir.run")))throw new IOException("Missing or invalid Maps/DM-Hildir.run.");
        for(String folder:new String[]{"Textures","Meshes","Sounds","Music"}){
            boolean found=false;File[] files=child(root,folder).listFiles();
            if(files!=null)for(File f:files)if(f.isFile()&&allowed(folder,f.getName())&&f.length()>32){found=true;break;}
            if(!found)throw new IOException("Missing game files in "+folder+".");
        }
        try{
            MessageDigest hash=MessageDigest.getInstance("SHA-1");byte[] b=new byte[65536];
            try(InputStream in=new FileInputStream(child(system,"Rune.exe"))){int n;while((n=in.read(b))!=-1)hash.update(b,0,n);}
            StringBuilder hex=new StringBuilder();for(byte value:hash.digest())hex.append(String.format(Locale.ROOT,"%02x",value&255));
            if(!Arrays.asList("2db721aa27f9a13c371de08af2c3fbb7ed0642f3","4a517c7f96a27cf7e25534c80d50af8db4065276","b644bb7f6ce07e284fb81e1ed496607f77080771").contains(hex.toString()))throw new IOException("Unsupported Rune executable. Use Rune Classic 1.10/1.11 or Rune Gold 1.07.");
        }catch(java.security.NoSuchAlgorithmException e){throw new IOException("Could not verify the Rune version.",e);}
    }
    static boolean ready(File root){try{validate(root);return true;}catch(IOException|SecurityException e){return false;}}
    static void prepare(File stage)throws IOException {
        File defaults=new File(stage,"System/Default.ini");
        String text=new String(Files.readAllBytes(defaults.toPath()),StandardCharsets.ISO_8859_1);
        StringBuilder out=new StringBuilder();String section="";boolean paths=false;
        for(String line:text.split("\\r?\\n")){
            String clean=lower(line.trim());if(clean.startsWith("["))section=clean;
            String key=clean.split("=",2)[0].trim();
            if(section.equals("[core.system]")){
                if(key.equals("savepath"))line="SavePath=..\\Save";
                else if(key.equals("cachepath"))line="CachePath=..\\Cache";
                else if(key.equals("paths")){
                    if(!paths)out.append("Paths=..\\System\\*.u\nPaths=..\\Maps\\*.run\nPaths=..\\Textures\\*.utx\nPaths=..\\Sounds\\*.uax\nPaths=..\\Music\\*.umx\nPaths=..\\Meshes\\*.ums\n");
                    paths=true;continue;
                }
            }
            out.append(line).append('\n');
        }
        try(FileOutputStream stream=new FileOutputStream(defaults)){stream.write(out.toString().getBytes(StandardCharsets.ISO_8859_1));stream.getFD().sync();}
        new File(stage,"Save").mkdir();new File(stage,"Cache").mkdir();validate(stage);
    }
    static void removeStage(File file)throws IOException {
        File[] files=file.listFiles();if(files!=null)for(File child:files)removeStage(child);
        if(file.exists()&&!file.delete())throw new IOException("Could not remove incomplete import: "+file.getName());
    }
}
