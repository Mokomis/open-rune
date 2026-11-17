#pragma once
#include <string>
#include <vector>

struct MenuSection {std::string id,title,category;std::vector<std::string> keys;std::string help;};
inline const std::vector<MenuSection>& RuneMenuSections(){
 static const std::vector<MenuSection> pages={
  {"display","Display & performance","Video",{"fullscreen","resolution","scalingMethod","aa","fps","pacing"},"Changes apply immediately. Scene resolution affects 3D rendering; menus stay at display resolution. GSR is software upscaling. Frame target is separate from display refresh."},
  {"camera","Camera & field of view","Video",{"fov","wideView"},"Automatic widescreen framing preserves vertical visibility. A higher field of view shows more, with smaller objects and stronger perspective near the edges."},
  {"lighting","Lighting & shadows","Video",{"pixelLighting","materialLighting","environmentProbes","ambientOcclusion","shadowLights"},"Ambient occlusion is Experimental. Extra shadow lights require Remastered lighting. Saved choices are kept when a dependency is inactive."},
  {"textures","Textures & surfaces","Video",{"texturePack","filter","materialMaps","detail","depth"},"WIP artwork is unfinished. Open Rune asks before downloading. Missing textures stay original. 2× and 4× describe texture size, not display scaling. Material maps require Remastered lighting."},
  {"effects","Effects & weather","Video",{"bloom","sharp","shadow","fireEffects","gore","snow","water"},"Visual changes only. Compare original visuals temporarily restores original presentation, retaining your choices and audio settings. Full scene water reflections cost more GPU time."},
  {"volume","Volume","Audio",{"master","music","effects"},"Changes apply immediately. Master affects all sound. Music and effects can be adjusted separately."},
  {"soundEffects","Sound effects","Audio",{"soundRemaster"},"WIP layered sounds require the optional sound pack. Restart Open Rune after changing this setting. Dialogue and music keep their original recordings; missing or incompatible effects use their originals."},
  {"spatial","Environmental & headphone audio","Audio",{"environment","hrtf"},"Environmental sound adds room reverb and muffling. Headphone spatial audio is intended for stereo headphones."},
  {"input","Look & keyboard","Controls",{"sensitivity","invert"},"Mouse and right-stick sensitivity and vertical inversion. The current keyboard bindings appear below."},
  {"gamepad","Controller","Controls",{},"The detected controller and bindings appear here. Feedback settings are under Controls → Vibration."},
  {"vibration","Vibration","Controls",{"rumble","rumbleStrength"},"Impact feedback uses supported XInput controllers. Vibration ends when paused or disconnected. Test vibration previews the selected intensity."},
  {"hud","HUD & FPS display","Interface",{"counter","playerNames"},"The engine FPS counter is separate from measured display cadence. Touch controls and HUD layout editing are available on Android; those unsupported controls are omitted here."},
  {"campaign","Campaign","Gameplay",{"difficulty"},"Difficulty applies to new campaigns. Loaded saves retain their difficulty. Character selection returns here when you finish browsing."},
  {"combat","Laws of Battle","Gameplay",{},"Campaign preferences apply locally; multiplayer uses host rules. Set normal-combat leap and rush under Match rules when hosting."},
  {"physics","Physics","Gameplay",{"physics"},"Use picks up loose props; Attack or Throw launches them. Existing corpse controls work with articulated bodies. Standard is the default, with stronger knockbacks and lively articulated bodies. Cinematic uses gentler forces. Legacy uses original gameplay physics. Multiplayer follows host rules."},
  {"connection","Connection smoothing","Connection",{"networkSmoothing"},"Adaptive varies a bounded visual buffer for remote movement. Local controls remain responsive and damage/blocking remain authoritative. Counters are under About & support → Diagnostics."},
  {"diagnostics","Diagnostics","Support",{"networkDiagnostics"},"Optional troubleshooting counters. Transmission timeouts are not a direct packet-loss measurement. Diagnostics do not change gameplay."},
  {"updates","Updates","Support",{},"Published stable releases only. Release versions are separate from network and gameplay compatibility. Open the release page for complete notes and downloads."},
  {"support","About & licenses","Support",{},"Open Rune is an independent SurrealEngine fork and requires your original game files. License notices and applicable source offers remain beside the executable."}
 };return pages;
}
