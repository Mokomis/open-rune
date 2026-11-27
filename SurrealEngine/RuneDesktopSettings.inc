// Names and feature groupings mirror Android/RuneSettings.java. Windows-specific support is explicit.
add("resolution","Rendering resolution","Percentage of display width and height. Lower values reduce GPU work.",{"Native (100%)","75%","50%","25%"},0,0,3);
add("scalingMethod","Scaling method","Reconstructs a lower-resolution scene using GSR on the GPU. Native (100%) bypasses upscaling. Menus remain full resolution.",{"Software upscaling (GSR)"},0,0,0);
add("aa","Anti-aliasing","Smooths geometry edges.",{"Off","2× MSAA","4× MSAA"},2,0,2);
add("fps","Frame rate target","Frame limit, independent of display refresh. Select to enter a custom value.",{},120,30,1000);
add("pacing","Frame pacing","Synchronizes presentation with the display.",{"Off","On"},1,0,1);
add("fov","Field of view","Base horizontal view at 4:3. Wider screens expand the sides automatically.",{},90,70,120);
add("wideView","Widescreen framing","Automatic preserves vertical visibility as the screen gets wider.",{"Original","Automatic"},1,0,1);
add("bloom","Bloom & color","Subtle highlight glow and warmer color tuning.",{"Original","Enhanced"},0,0,1);
add("fireEffects","Fire & combat effects","Fuller flames, rising embers and motion-aligned sparks, debris and blood droplets. Includes outdoor breath for living characters. Cosmetic only, with a shared particle budget.",{"Original","Enhanced"},1,0,1);
add("gore","Gore presentation","Enhanced adds cosmetic tumbling fragments, wound shading, impact blood and corpse settling. Pickupable limbs and match rules stay unchanged.",{"Original","Enhanced"},1,0,1);
add("snow","Snowfall override","Automatic follows changing outdoor weather; multiplayer follows the host. Always Off disables snowfall. Other choices force a local snow level without changing anyone else. Roofs provide shelter.",{"Automatic","Always Off","Always Light","Always Heavy"},0,0,3);
add("sharp","Adaptive sharpening","Restores fine contrast while limiting bright edge halos.",{"Off","Low","Medium","High"},1,0,3);
add("filter","Texture filtering","Keeps angled textures clear; limited by GPU support.",{"8×","16×"},1,0,1);
add("shadow","Shadow / midtone lift","Reveals dark detail while preserving deep blacks.",{"Off","Low","Medium","High"},1,0,3);
add("pixelLighting","Lighting mode","Remastered adds nearby moving shadows, stronger light and material highlights.",{"Original","Per-pixel","Remastered"},0,0,2);
add("materialLighting","Material lighting","Physical adds roughness-aware GGX highlights and metal response. Requires Remastered lighting.",{"Classic","Physical"},0,0,1);
add("environmentProbes","Local environment lighting","Baked room color and broad reflections. Static scenery only; prepared when first enabled. Requires Remastered lighting.",{"Off","On"},0,0,1);
add("materialMaps","Material maps","Generated normal and roughness detail for Remastered lighting.",{"Original","Enhanced"},0,0,1);
add("texturePack","Texture artwork","Work-in-progress remastered artwork. Unfinished or unavailable textures keep their original artwork.",{"Original","WIP 2×","WIP 4×"},0,0,2);
#ifdef _WIN32
add("water","Water","Enhanced adds flowing ripples, depth colour, glints and gentle underwater distortion. Legacy reflections redraw the scene and cost more.",{"Original","Enhanced","Legacy reflections"},0,0,2);
#else
add("water","Water reflections","Approximate adds ripples and an environment sheen. Full scene reflects nearby scenery at higher cost.",{"Original","Approximate","Full scene"},0,0,2);
#endif
add("shadowLights","Shadow-casting lights","Maximum nearby shadow-casting lights. Higher counts cost more rendering time. Requires Remastered lighting.",{"One nearby","Three nearby","Four nearby","Six nearby","Eight nearby"},0,0,4);
add("ambientOcclusion","Ambient occlusion","Experimental contact shading around corners and objects. Off is recommended at native resolution until this effect is faster.",{"Off","Low","Medium","High"},0,0,3);
add("detail","Surface detail","Generated relief on stone and wood. High strengthens shading.",{"Off","Low","Medium","High"},0,0,3);
add("depth","Surface depth","Parallax on stone and wood. High uses 16 steps and costs more GPU time.",{"Off","Low","Medium","High"},0,0,3);
add("soundRemaster","Sound effects","Layered gameplay sounds (WIP). Restart Open Rune to apply. Missing or incompatible replacements use the original sound.",{"Original","Remastered (WIP)"},0,0,1);
add("environment","Environmental sound","Room reverb and sound muffling through walls.",{"Off","On"},1,0,1);
add("hrtf","Headphone spatial audio","Directional binaural audio; best with stereo headphones.",{"Off","On"},0,0,1);
add("master","Master volume","Overall game volume.",{},100,0,100);
add("music","Music volume","Music and ambient score.",{},100,0,100);
add("effects","Effects volume","Combat, voices and environmental sounds.",{},100,0,100);
add("sensitivity","Look sensitivity","Mouse and right-stick camera speed.",{"Very low","Low","Normal","High","Very high"},2,0,4);
add("invert","Invert vertical look","Reverse vertical mouse and right-stick look.",{"Off","On"},0,0,1);
add("rumble","Rumble","Impact feedback on connected Windows XInput controllers.",{"Off","On"},1,0,1);
add("rumbleStrength","Rumble intensity","Scales both controller motors.",{},65,0,100);
add("hudStyle","Gameplay HUD","Choose the health, shield and power meters shown during play.",{"Original","Open Rune"},1,0,1);
add("counter","Show FPS","Displays engine FPS; actual display timing is measured separately.",{"Off","On"},1,0,1);
add("networkDiagnostics","Network diagnostics","Multiplayer frame times, acknowledged round-trip time, snapshot gaps and transport timeouts. Timeouts are not a packet-loss measurement.",{"Off","On"},0,0,1);
add("playerNames","Player names","Show names above players and bots. Team markers stay visible when names are hidden. The scoreboard always shows names and bot labels.",{"Off","On"},1,0,1);
add("networkSmoothing","Network smoothing","Adaptive smooths remote movement against packet jitter with a bounded buffer. Local controls stay responsive; gameplay remains server controlled.",{"Original","Adaptive"},1,0,1);
add("kick","Combat kicks","F / right-stick click. Raised shields absorb damage and most knockback.",{"Off","On"},1,0,1);
add("bash","Shield bash","Double-tap Block. Brief stagger and small shove; raised shields counter it.",{"Off","On"},1,0,1);
add("bloodlustMoves","Giant leap & shield rush","Giant leap is multiplayer-only: double-tap Jump. Shield rush works in campaign and multiplayer: hold Block and press Attack. Available without bloodlust by default, with a shared 4-second cooldown. Rush requires a shield. Eligible enemies and bots can use them too. Multiplayer follows the host.",{"Off","On"},1,0,1);
add("dualWield","Dual wield","Use two light weapons and a weaker crossed block. Your shield stays in inventory; turn this off to use it again. Compatible Vikings can dual wield too.",{"Off","On"},1,0,1);
add("tactics","Enemy tactics","Tactical humanoids use spacing, delayed defense, kicks and shield bashes. Bosses and scripted scenes keep their original behavior.",{"Original","Tactical"},1,0,1);
add("difficulty","New campaign difficulty","Applies when you start a new campaign. Saves retain their difficulty.",{"Easy","Medium","Hard","Very hard"},1,0,3);
add("fullscreen","Window mode","Switch between a normal window and fullscreen.",{"Windowed","Fullscreen"},0,0,1);

add("normalBloodlust","Leap & rush availability","Normal combat is the default: neither move needs or spends bloodlust. The shared 4-second cooldown and shield requirement remain. Choose Bloodlust only to restore the old requirement. Multiplayer follows the host.",{"Bloodlust only","Normal combat"},1,0,1);

add("physics","Physics mode","Standard uses energetic articulated bodies, recoverable knockdowns, hit reactions and throwable props. Cinematic softens forces. Legacy uses original gameplay physics. Bosses and scripted scenes stay protected. Multiplayer follows host rules.",{"Legacy","Cinematic","Standard"},2,0,2);
