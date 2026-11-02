package org.runeclassic.port;

import java.io.IOException;
import java.nio.charset.StandardCharsets;

/** Validate imports before replacing a remembered invitation or a live campaign. */
final class RuneInvitation {
    static byte[] normalize(byte[] data) throws IOException {
        if(data.length>1024)throw new IOException("Invitation is too large.");
        String text=new String(data,StandardCharsets.UTF_8).replace("\r\n","\n").trim();
        String[] t=text.split("\\s+");
        try{
            if(t.length<8||!t[0].matches("OPEN-RUNE-INVITE-[1-4]"))throw new IllegalArgumentException();
            int version=t[0].charAt(t[0].length()-1)-'0';
            if(t.length!=(version==4?13:7+version))throw new IllegalArgumentException();
            String[] ip=t[1].split("\\.",-1);if(ip.length!=4)throw new IllegalArgumentException();
            for(String part:ip){if(!part.matches("[0-9]{1,3}"))throw new IllegalArgumentException();number(part,0,255);}
            if(t[1].equals("0.0.0.0")||Integer.parseInt(ip[0])>=224)throw new IllegalArgumentException();
            number(t[2],1,65535);if(!t[3].matches("(DM-|AR-|HB-)[A-Za-z0-9_-]{1,45}"))throw new IllegalArgumentException();
            long slot=number(t[4],1,16);number(t[5],1,4294967295L);
            if(!t[6].matches("[0-9a-f]{32}")||!t[7].matches("[0-9a-f]{64}"))throw new IllegalArgumentException();
            long players=version>=2?number(t[8],2,16):2;if(slot>players)throw new IllegalArgumentException();
            if(version>=3)number(t[9],1,4);
            if(version==4){number(t[10],2,4);number(t[11],1,4);number(t[12],0,1);}
            // The native reader requires the magic on its own line; retain its
            // whitespace format and normalize Windows line endings only.
            if(!text.startsWith(t[0]+"\n"))throw new IllegalArgumentException();
            return (text+"\n").getBytes(StandardCharsets.UTF_8);
        }catch(IllegalArgumentException e){throw new IOException("Choose a valid Open Rune invitation from the host.");}
    }
    private static long number(String value,long min,long max){
        if(!value.matches("[0-9]+"))throw new IllegalArgumentException();long n=Long.parseLong(value);
        if(n<min||n>max)throw new IllegalArgumentException();return n;
    }
}
