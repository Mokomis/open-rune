package org.runeclassic.port;

final class RuneControllerMath {
    static float deadzone(float value,float flat){
        flat=Math.min(.95f,Math.max(0,flat));
        if(!Float.isFinite(value)||Math.abs(value)<=flat)return 0;
        return Math.copySign(Math.min(1,(Math.abs(value)-flat)/(1-flat)),value);
    }
    static boolean trigger(float value,boolean held){return value>(held?.35f:.55f);}
}
