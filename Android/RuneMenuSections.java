package org.runeclassic.port;

/** Navigation only: existing preference keys and values are deliberately retained. */
final class RuneMenuSections {
    static String[] sections(String category){switch(category){
    case "Video":return new String[]{"display","camera","lighting","textures","effects"};
    case "Audio":return new String[]{"volume","soundEffects","spatial"};
    case "Controls":return new String[]{"input","touch","gamepad","vibration"};
    case "Interface":return new String[]{"hud"};
    case "Gameplay":return new String[]{"campaign","combat","enemies","physics"};
    case "Connection":return new String[]{"connection"};
    default:return new String[]{"updates","diagnostics","support"};}}
    static String title(String page){switch(page){
    case "display":return "Display & performance";case "camera":return "Camera & field of view";
    case "lighting":return "Lighting & shadows";case "textures":return "Textures & surfaces";
    case "effects":return "Effects & weather";case "volume":return "Volume";
    case "soundEffects":return "Sound effects";case "spatial":return "Environmental & headphone audio";case "input":return "Look & keyboard";
    case "touch":return "Touch controls";case "gamepad":return "Controller";
    case "vibration":return "Vibration";case "hud":return "HUD & FPS display";
    case "campaign":return "Campaign";case "combat":return "Laws of Battle";
    case "physics":return "Physics";case "enemies":return "Enemy behavior";case "connection":return "Connection smoothing";
    case "updates":return "Updates";case "diagnostics":return "Diagnostics";default:return "About & licenses";}}
    static String[] keys(String page){switch(page){
    case "display":return new String[]{"resolution","scalingMethod","aa","fps","refresh","pacing","fpsManual","refreshManual"};
    case "camera":return new String[]{"fov","wideView"};
    case "lighting":return new String[]{"pixelLighting","materialLighting","environmentProbes","ambientOcclusion","shadowLights"};
    case "textures":return new String[]{"texturePack","filter","materialMaps","detail","depth"};
    case "effects":return new String[]{"bloom","sharp","shadow","fireEffects","gore","snow","water"};
    case "volume":return new String[]{"master","music","effects"};
    case "soundEffects":return new String[]{"soundRemaster"};
    case "spatial":return new String[]{"environment","hrtf"};
    case "input":return new String[]{"sensitivity","invert"};
    case "touch":return new String[]{"touch","opacity"};
    case "vibration":return new String[]{"rumble","rumbleOutput","rumbleStrength"};
    case "hud":return new String[]{"hud","counter","playerNames"};
    case "campaign":return new String[]{"difficulty"};
    case "combat":return new String[]{};
    case "physics":return new String[]{"physics"};case "enemies":return new String[]{"tactics"};
    case "connection":return new String[]{"networkSmoothing"};
    case "diagnostics":return new String[]{"networkDiagnostics"};
    default:return new String[0];}}
    static String help(String page){switch(page){
    case "display":return "Changes apply immediately.\n\nRendering resolution changes the 3D scene. Menus and HUD stay sharp.\n\nFrame rate target and display refresh are separate controls.";
    case "soundEffects":return "Layered gameplay sounds are WIP and require the optional sound pack.\n\nRestart Open Rune after changing this setting. Music and dialogue keep their original recordings. Missing or incompatible effects use the original sound.";
    case "lighting":return "Lighting mode sets the overall style.\n\nAmbient occlusion is Experimental.\n\nAdditional shadow lights require Remastered lighting.";
    case "textures":return "Select WIP 2× or WIP 4× to download the shared texture pack. Open Rune asks before downloading. Missing textures stay original.\n\n2× and 4× describe texture size, not display scaling.\n\nMaterial maps require Remastered lighting.";
    case "effects":return "Visual changes only.\n\nCompare original visuals temporarily restores original presentation without overwriting your choices.\n\nFull scene water reflections cost more GPU time.";
    case "campaign":return "Difficulty applies to a new campaign. Loaded saves retain their difficulty.\n\nChoose any installed character for your next campaign session.";
    case "combat":return "Campaign preferences apply locally. Multiplayer uses the host’s combat rules.\n\nNormal-combat leap and rush belong in Match rules when hosting.";
    case "physics":return "Use picks up nearby loose props. Attack or Throw launches a held prop. Existing corpse controls also work with articulated bodies.\n\nStandard is the default, with stronger knockbacks and lively articulated bodies. Cinematic uses gentler forces. Legacy uses original gameplay physics. The host chooses online.";
    case "enemies":return "Tactical humanoids use spacing, defense and combat moves.\n\nBosses, scripted scenes and other creatures retain their original behavior. Multiplayer bots use host rules.";
    case "connection":return "Adaptive varies a short visual buffer for remote movement.\n\nYour controls remain responsive. Damage and blocking remain host-controlled.\n\nDetailed counters are under About & support → Diagnostics.";
    case "touch":return "Touch controls automatically hide when a controller is connected. The Menu button stays available. Double-tap the center of the right camera stick to jump.\n\nThe layout editor has its own Save layout and Cancel actions.";
    case "hud":return "Reposition and resize individual HUD elements in the editor. Bars can be horizontal or vertical.\n\nLayout changes are saved only with Save layout.";
    case "diagnostics":return "Optional troubleshooting information.\n\nTransmission timeouts are not a direct packet-loss measurement. Diagnostics do not change gameplay.";
    case "gamepad":return "The detected controller and current bindings are shown here.\n\nFor feedback strength and output, open Controls → Vibration.";
    case "updates":return "Published stable releases only.\n\nRelease versions are separate from network and gameplay compatibility. Open the release page for complete notes and downloads.";
    case "support":return "Open Rune is an independent SurrealEngine fork. Original game assets remain the property of their owners.\n\nDeveloper tools are separated from normal play.";
    default:return "Changes apply immediately.\n\nSelect a setting for its explanation. Back returns to the previous screen without undoing saved choices.";}}
}
