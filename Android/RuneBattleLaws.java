package org.runeclassic.port;
/** Laws share their bit order and descriptions with the native authority. */
final class RuneBattleLaws {
 static final int ALL=4095, LEAP=16, DUAL=4, CROSS=8, NORMAL=2048;
 static final String[] NAMES={"Kick","Shield bash","Dual wield","Crossed-weapon block","Giant leap","Shield rush","Two-handed aerial slam","Corpse pickup & throw","Loose prop interaction","Physical knockdowns","Tactical campaign enemies","Special moves without rage"};
 static final String[] HELP={"F / right-stick click / Kick button. Faster close-range kick; raised shields absorb most force.","Double-tap Block with a shield. Shield swipe, brief stun and head recoil on a clean hit.","Pick up a second compatible one-handed weapon with Use; Attack alternates weapons. Shields stay stowed.","Hold Block while dual wielding. Weaker protection than a shield; requires Dual wield.","Double-tap Jump in a direction. Multiplayer only; always disabled for everyone in the campaign.","Hold Block and press Attack with a shield. Stow the weapon and charge forward; knockdowns follow the physical-knockdown law.","Jump, tap Jump again while airborne, then Attack with a two-handed weapon. Flip upward and slam down hard.","Use near an intact, finished corpse to lift it. Attack or Throw launches it; Use drops it. Eligible humanoids can do this too.","Use lifts safe loose stools, kegs and buckets; Attack or Throw launches them; Use drops them. Kick can shove them. Requires Cinematic or Chaos physics; scripted scenery stays protected.","Strong impacts can knock eligible living fighters into recoverable ragdolls. Shield rush can do this even with Original physics. No extra control; bosses and scripted actors stay protected.","Eligible campaign humanoids use spacing, defense and permitted special moves. Multiplayer bots always obey the selected laws.","Leap and shield rush need no bloodlust. Uncheck to require bloodlust; their cooldowns and individual checkboxes still apply."};
 static int campaign(RuneSettings s){
  if(!s.prefs.contains("laws.campaign")){
   int m=ALL&~LEAP;
   String[] old={"kick","bash","dualWield"};
   for(int i=0;i<old.length;i++)if(s.get(old[i])==0)m&=~(1<<i);
   if(s.get("bloodlustMoves")==0)m&=~32;
   if(s.get("tactics")==0)m&=~1024;
   if(s.get("normalBloodlust")==0)m&=~NORMAL;
   s.prefs.edit().putInt("laws.campaign",m).apply();
  }
  return s.prefs.getInt("laws.campaign",ALL)&ALL&~LEAP;
 }
 static int effective(int m){return (m&DUAL)==0?m&~CROSS:m;}
 static void campaign(RuneSettings s,int mask){s.prefs.edit().putInt("laws.campaign",mask&ALL&~LEAP).apply();if(s.listener!=null)s.listener.changed("laws");}
}
