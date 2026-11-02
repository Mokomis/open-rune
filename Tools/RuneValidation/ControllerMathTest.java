package org.runeclassic.port;

/** Host-side checks against the Kishi's measured neutral drift and trigger range. */
public final class ControllerMathTest {
    static void check(boolean ok,String message){if(!ok)throw new AssertionError(message);}
    public static void main(String[] args){
        check(RuneControllerMath.deadzone(-.10f,.16f)==0,"Kishi neutral drift must not turn camera");
        check(RuneControllerMath.deadzone(.03f,.16f)==0,"Kishi neutral vertical drift");
        check(RuneControllerMath.deadzone(.2f,.25f)==0,"Honor device dead zone");
        check(RuneControllerMath.deadzone(1,.16f)==1&&RuneControllerMath.deadzone(-1,.16f)==-1,"Full travel preserved");
        check(Math.abs(RuneControllerMath.deadzone(.58f,.16f)-.5f)<.0001f,"Rescale remaining travel");
        check(RuneControllerMath.deadzone(Float.NaN,.16f)==0,"Reject invalid samples");
        check(!RuneControllerMath.trigger(.5f,false),"Noise must not press trigger");
        check(RuneControllerMath.trigger(.6f,false),"Trigger presses above threshold");
        check(RuneControllerMath.trigger(.5f,true),"Trigger hysteresis prevents repeated block taps");
        check(!RuneControllerMath.trigger(.3f,true),"Trigger releases below threshold");
        check(RuneController.mapping(96)==62,"A jumps");
        check(RuneController.mapping(99)==66&&RuneController.mapping(105)==66,"X and RT attack");
        check(RuneController.mapping(102)==114&&RuneController.mapping(104)==114,"LB and LT block");
        check(RuneController.mapping(100)==113&&RuneController.mapping(106)==113,"Y and L3 use");
        check(RuneController.mapping(107)==34,"R3 kicks");
        check(RuneController.mapping(109)==33,"Select activates Rune Power");
        System.out.println("PASS: measured stick drift, device dead zone, full travel, normalization, trigger hysteresis");
    }
}
