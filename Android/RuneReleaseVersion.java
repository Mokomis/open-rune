package org.runeclassic.port;
import java.util.*;
/** SemVer precedence shared in behavior with the native release checker. */
final class RuneReleaseVersion implements Comparable<RuneReleaseVersion> {
    final long[] core=new long[3];String[] pre=new String[0];
    static RuneReleaseVersion parse(String text){
        if(text==null||text.length()==0||text.length()>96)throw new IllegalArgumentException("Invalid version");
        String s=text.startsWith("v")?text.substring(1):text;String[] meta=s.split("\\+",-1);
        if(meta.length>2)throw new IllegalArgumentException("Invalid metadata");
        if(meta.length==2)ids(meta[1],false);s=meta[0];
        int dash=s.indexOf('-');RuneReleaseVersion out=new RuneReleaseVersion();
        if(dash>=0){out.pre=ids(s.substring(dash+1),true);s=s.substring(0,dash);}
        String[] numbers=s.split("\\.",-1);if(numbers.length!=3)throw new IllegalArgumentException("Invalid core");
        for(int i=0;i<3;i++){if(!numbers[i].matches("0|[1-9][0-9]*"))throw new IllegalArgumentException("Invalid number");out.core[i]=Long.parseLong(numbers[i]);if(out.core[i]>4294967295L)throw new IllegalArgumentException("Overflow");}
        return out;
    }
    private static String[] ids(String text,boolean prerelease){
        String[] out=text.split("\\.",-1);for(String id:out)if(!id.matches("[0-9A-Za-z-]+")||(prerelease&&id.matches("[0-9]+")&&id.length()>1&&id.startsWith("0")))throw new IllegalArgumentException("Invalid identifier");return out;
    }
    public int compareTo(RuneReleaseVersion other){
        for(int i=0;i<3;i++)if(core[i]!=other.core[i])return Long.compare(core[i],other.core[i]);
        if(pre.length==0||other.pre.length==0)return pre.length==0?(other.pre.length==0?0:1):-1;
        for(int i=0;i<Math.min(pre.length,other.pre.length);i++){
            String a=pre[i],b=other.pre[i];if(a.equals(b))continue;boolean an=a.matches("[0-9]+"),bn=b.matches("[0-9]+");
            if(an!=bn)return an?-1:1;if(an&&a.length()!=b.length())return Integer.compare(a.length(),b.length());return a.compareTo(b);
        }return Integer.compare(pre.length,other.pre.length);
    }
}
