package org.runeclassic.port;
/** Runs on a host JDK without Android or a device. */
public final class ReleaseVersionTest {
 public static void main(String[] args){
  int checks=0;
  String[] bad={"","1","1.2","1.2.3.4","01.2.3","1.02.3","1.2.03","1.2.3-","1.2.3-a..b","1.2.3-01","1.2.3+","1.2.3+a..b","1.2.3/evil","999999999999999.0.0"};
  for(String text:bad){boolean rejected=false;try{RuneReleaseVersion.parse(text);}catch(IllegalArgumentException e){rejected=true;}if(!rejected)throw new AssertionError(text);checks++;}
  String[] ordered={"1.0.0-alpha","1.0.0-alpha.1","1.0.0-alpha.beta","1.0.0-beta","1.0.0-beta.2","1.0.0-beta.11","1.0.0-rc.1","1.0.0","1.0.1","1.1.0","2.0.0"};
  for(int i=1;i<ordered.length;i++){if(RuneReleaseVersion.parse(ordered[i-1]).compareTo(RuneReleaseVersion.parse(ordered[i]))>=0)throw new AssertionError(ordered[i]);checks++;}
  if(RuneReleaseVersion.parse("v1.2.3+a").compareTo(RuneReleaseVersion.parse("1.2.3+b"))!=0)throw new AssertionError("metadata");
  System.out.println("ANDROID_RELEASE_VERSION_OK checks="+(checks+1));
 }
}
