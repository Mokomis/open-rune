#include "../SurrealEngine/RuneBattleLaws.h"
#include "RuneSnow.h"
#include "RuneController.h"
#include "RuneVisualEffects.h"
#include "RuneGore.h"
#include "RunePhysics.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "RuneBloodlust.h"
#include "RuneTactics.h"
#include "RuneDualWield.h"
#include "RuneFeedback.h"
#include "Precomp.h"
#include "GameApp.h"
#include "Utils/File.h"
#include "RenderDevice/VisualComparison.h"
#include "Render/ShadowSettings.h"
#include "RuneCombat.h"
#include "RuneMenu.h"
#include "RuneCharacters.h"
#include "RuneCharacterPreview.h"
#include "RuneView.h"
#include "RuneHud.h"
#include "RuneMultiplayer.h"
#include <jni.h>
#include <SDL2/SDL_system.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>

extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeSetEnhanced(JNIEnv*, jclass, jboolean enabled)
{
	VisualComparison::RequestedMode.store(enabled ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeFeedbackEnabled(JNIEnv*,jclass,jboolean enabled,jboolean trace) {
    RuneFeedback::Trace.store(trace);GameFeedback::SetEnabled(enabled);
}
extern "C" JNIEXPORT jlong JNICALL Java_org_runeclassic_port_MainActivity_nativeFeedbackPoll(JNIEnv*,jclass) {
    return static_cast<jlong>(GameFeedback::Pack(GameFeedback::Poll()));
}
extern "C" JNIEXPORT jlong JNICALL Java_org_runeclassic_port_MainActivity_nativeFeedbackPreview(JNIEnv*,jclass) {
    return static_cast<jlong>(GameFeedback::Pack(GameFeedback::Describe(GameFeedback::Event::Damage)));
}

extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeNewFight(JNIEnv*, jclass)
{
	RuneCombat::Request(5);
}

extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeSetWorldFeatures(JNIEnv*, jclass, jint settings)
{
    VisualComparison::RequestedWorldFeatures.store(settings & 262143);
}
extern "C" JNIEXPORT jint JNICALL Java_org_runeclassic_port_MainActivity_nativeGetWorldFeatures(JNIEnv*, jclass)
{
    return VisualComparison::AppliedWorldFeatures.load();
}

extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeSetShadowBudget(JNIEnv*, jclass, jint budget)
{
    RuneShadows::RequestedBudget.store(std::clamp(int(budget), 1, RuneShadows::Capacity));
}

extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeSetTexturePack(JNIEnv*, jclass, jint quality)
{
    VisualComparison::RequestedTexturePack.store(std::clamp(int(quality),0,2));
}
extern "C" JNIEXPORT jstring JNICALL Java_org_runeclassic_port_MainActivity_nativeTexturePackStatus(JNIEnv* env, jclass)
{
    auto quality=VisualComparison::AppliedTexturePack.load();
    std::string status=quality==0?"Original artwork":(quality==1?"WIP textures · 2×":"WIP textures · 4×");
    if(quality) status+="\n"+std::to_string(VisualComparison::TexturePackLoaded.load())+" maps loaded · "+
        std::to_string(VisualComparison::TexturePackBytes.load()/(1024*1024))+" MB / 128 MB";
    return env->NewStringUTF(status.c_str());
}

extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeSetClarity(JNIEnv*, jclass, jint settings)
{
	VisualComparison::RequestedClarity.store(settings);
}

extern "C" JNIEXPORT jint JNICALL Java_org_runeclassic_port_MainActivity_nativeGetClarity(JNIEnv*, jclass)
{
	return VisualComparison::AppliedClarity.load();
}

extern "C" JNIEXPORT jboolean JNICALL Java_org_runeclassic_port_MainActivity_nativeCombatReady(JNIEnv*, jclass)
{
	return RuneCombat::Ready.load();
}

extern "C" JNIEXPORT jboolean JNICALL Java_org_runeclassic_port_MainActivity_nativePlayerDead(JNIEnv*, jclass)
{
    return RuneMenu::PlayerDead.load();
}

extern "C" JNIEXPORT jboolean JNICALL Java_org_runeclassic_port_MainActivity_nativePracticeArena(JNIEnv*, jclass)
{
    return RuneCombat::PracticeArena.load();
}

extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeSessionAction(JNIEnv*, jclass, jint action)
{
	if (action>=1 && action<=4) RuneCombat::Request(action);
}
extern "C" JNIEXPORT jint JNICALL Java_org_runeclassic_port_MainActivity_nativeSessionResult(JNIEnv*, jclass)
{
	return RuneCombat::SessionResult.exchange(0);
}

extern "C" JNIEXPORT jint JNICALL Java_org_runeclassic_port_MainActivity_nativeGetVisualMode(JNIEnv*, jclass)
{
	return VisualComparison::AppliedMode.load();
}

extern "C" JNIEXPORT jfloat JNICALL Java_org_runeclassic_port_MainActivity_nativeGetEngineFps(JNIEnv*, jclass)
{
	return VisualComparison::EngineFps.load();
}
extern "C" JNIEXPORT jfloat JNICALL Java_org_runeclassic_port_MainActivity_nativeGetViewFov(JNIEnv*, jclass)
{
    return RuneView::AppliedFov.load();
}

extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeSetUpscaled(JNIEnv*, jclass, jboolean enabled)
{
	VisualComparison::RequestedUpscale.store(enabled ? 1 : 0);
}
extern "C" JNIEXPORT jint JNICALL Java_org_runeclassic_port_MainActivity_nativeGetUpscaleMode(JNIEnv*, jclass)
{
	return VisualComparison::AppliedUpscale.load();
}

extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeSetUpscaleMode(JNIEnv*, jclass, jint mode)
{
    VisualComparison::RequestedUpscale.store(std::clamp((int)mode,0,3));
}
extern "C" JNIEXPORT jlong JNICALL Java_org_runeclassic_port_MainActivity_nativeScalingSnapshot(JNIEnv*, jclass)
{
    return static_cast<jlong>(VisualComparison::ScalingSnapshot.load());
}

extern "C" JNIEXPORT jboolean JNICALL Java_org_runeclassic_port_MainActivity_nativeSetPlayerName(JNIEnv* env,jclass,jstring value){
    if(!value)return false;const char* text=env->GetStringUTFChars(value,nullptr);if(!text)return false;bool ok=RuneMultiplayer::SetPlayerName(text);env->ReleaseStringUTFChars(value,text);return ok;
}

// Setting IDs are shared with RuneSettings.java. Only atomics cross JNI.
extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeMenuSetting(JNIEnv*, jclass, jint id, jint value)
{
    switch(id) {
    case 0: RuneMenu::Open.store(value != 0); break;
    case 1: RuneMenu::Antialias.store(std::clamp((int)value,0,2)); break;
    case 2: RuneMenu::TargetFps.store(std::clamp((int)value,30,1000)); break;
    case 3: RuneMenu::Pacing.store(value != 0); break;
    case 4: RuneMenu::Master.store(std::clamp((int)value,0,100)); break;
    case 5: RuneMenu::Music.store(std::clamp((int)value,0,100)); break;
    case 6: RuneMenu::Effects.store(std::clamp((int)value,0,100)); break;
    case 8: RuneHud::Enabled.store(value != 0); break;
    case 9: RuneKick::Enabled.store(value != 0); break;
    case 10: RuneBash::Enabled.store(value != 0); break;
    case 11: RuneView::FieldOfView.store(std::clamp((int)value,70,120)); break;
    case 12: RuneView::AutomaticAspect.store(value != 0); break;
    case 13: RuneTactics::Enabled.store(value != 0); break;
    case 21: RuneBloodlust::AllowNormal.store(value != 0); break;
    case 22: RuneMultiplayer::Diagnostics.store(value != 0); break;
    case 27: RuneMultiplayer::ShowPlayerNames.store(value != 0); break;
    case 23: RuneMultiplayer::AdaptiveSmoothing.store(value != 0); break;
    case 15: RuneBloodlust::Enabled.store(value != 0); break;
    case 16: RuneVisualEffects::Enabled.store(value != 0); break;
    case 24: RuneGore::Enabled.store(value != 0); break;
    case 26: RuneBattleLaws::Campaign=uint32_t(value)&RuneBattleLaws::All&~RuneBattleLaws::Leap;break;
    case 25: RunePhysics::RequestedMode=std::clamp((int)value,0,2); break;
    case 18: RuneSnow::Mode.store(RuneSnow::ClampMode((int)value)); break;
    case 19: case 20: {
        openrune::CharacterChoice choice{uint8_t(value&255),uint8_t((value>>8)&255)};
        if((id==20 && value==-1) || (value>=0 && value<=65535 && openrune::validCharacter(choice)))
            (id==19?RuneCharacters::Multiplayer:RuneCharacters::Campaign).store(value);
        break;
    }
    case 17: RuneMenu::Covered.store(value != 0); break;
    case 14: RuneDualWield::Enabled.store(value != 0); break;
    case 7: RuneMenu::Difficulty.store(std::clamp((int)value,-2,3)); break;
    }
}
extern "C" JNIEXPORT jint JNICALL Java_org_runeclassic_port_MainActivity_nativeBloodlustMoves(JNIEnv*,jclass) {return RuneCombat::Ready.load()?RuneBloodlust::ReadyMask.load():0;}
extern "C" JNIEXPORT jstring JNICALL Java_org_runeclassic_port_MainActivity_nativeHudSnapshot(JNIEnv* env, jclass, jfloatArray output)
{
    std::lock_guard<std::mutex> lock(RuneHud::Mutex);
    if (output && env->GetArrayLength(output) >= 12)
        env->SetFloatArrayRegion(output, 0, 12, RuneHud::Values.data());
    return env->NewStringUTF((RuneHud::Messages+"\x1f"+RuneHud::Score).c_str());
}
extern "C" JNIEXPORT jstring JNICALL Java_org_runeclassic_port_MainActivity_nativeMenuMetadata(JNIEnv* env, jclass)
{
    std::lock_guard<std::mutex> lock(RuneMenu::MetadataMutex);
    return env->NewStringUTF(RuneMenu::Metadata.c_str());
}
extern "C" JNIEXPORT jint JNICALL Java_org_runeclassic_port_MainActivity_nativeMenuVolume(JNIEnv*, jclass, jint kind)
{
    return kind==0 ? RuneMenu::ActualMusic.load() : RuneMenu::ActualEffects.load();
}
extern "C" JNIEXPORT jboolean JNICALL Java_org_runeclassic_port_MainActivity_nativeMenuSession(JNIEnv*, jclass, jint action, jint slot)
{
    if (RuneMultiplayer::Active.load() || action<1 || action>9 || slot<0 || slot>10 || !RuneCombat::Ready.load()) return false;
    RuneMenu::Slot.store(slot);
    return RuneCombat::Request(action);
}

extern "C" JNIEXPORT jboolean JNICALL Java_org_runeclassic_port_MainActivity_nativeMultiplayerWaiting(JNIEnv*,jclass) {return RuneMultiplayer::Waiting.load();}
extern "C" JNIEXPORT jboolean JNICALL Java_org_runeclassic_port_MainActivity_nativeMultiplayerActive(JNIEnv*,jclass) {return RuneMultiplayer::Active.load();}
extern "C" JNIEXPORT jboolean JNICALL Java_org_runeclassic_port_MainActivity_nativeMultiplayerJoin(JNIEnv* env,jclass,jstring file) {
#ifdef OPENRUNE_ENABLE_NETWORK
    if(!file)return false;
    const char* chars=env->GetStringUTFChars(file,nullptr);if(!chars)return false;
    std::string path=chars;env->ReleaseStringUTFChars(file,chars);return RuneMultiplayer::Join(path);
#else
    RuneMultiplayer::SetStatus("Networking is not enabled in this build");return false;
#endif
}
extern "C" JNIEXPORT jstring JNICALL Java_org_runeclassic_port_MainActivity_nativeMultiplayerStatus(JNIEnv* env,jclass) {return env->NewStringUTF(RuneMultiplayer::GetStatus().c_str());}
extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeMultiplayerAction(JNIEnv*,jclass,jint action) {
    if(RuneMultiplayer::Active.load()){if(action==1)RuneMultiplayer::Leave=true;if(action==2)RuneMultiplayer::Respawn=true;}
}

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc,char** argv)
{
	const char* storage=SDL_AndroidGetInternalStoragePath();
	if(!storage) return 1;
	std::string files=storage;
	int log=open((files+"/native-run.log").c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
	if(log>=0){dup2(log,STDOUT_FILENO);dup2(log,STDERR_FILENO);close(log);}
	setvbuf(stdout,nullptr,_IOLBF,0);
	try
	{
		std::string config=files+"/config";
		Directory::create(config+"/SurrealEngine");
		setenv("SURREALENGINE_CONFIG_DIR",config.c_str(),1);
		setenv("SURREALENGINE_RESOURCE_DIR",files.c_str(),1);
		setenv("SURREALWIDGETS_DISPLAY_BACKEND","SDL2",1);
		Array<std::string> args;
		setenv("SURREAL_ANDROID_FRAME_PACING", "1", 1);
		for(int i=1;i<argc;i++) {
			std::string arg=argv[i];
			if(arg.starts_with("--android-vulkan-driver=")) {setenv("OPENRUNE_VULKAN_DRIVER",arg.substr(24).c_str(),1);continue;}
			if(arg.starts_with("--android-native-lib-dir=")) {
				auto libs=arg.substr(25);
				setenv("OPENRUNE_NATIVE_LIB_DIR",libs.c_str(),1);
				setenv("SDL_VULKAN_LIBRARY",(libs+"libopenrune_vulkan.so").c_str(),1);continue;
			}
			args.push_back(argv[i]);
			if (args.back() == "--frame-pacing=0") setenv("SURREAL_ANDROID_FRAME_PACING", "0", 1);
		}
		GameApp app;
		return app.main(std::move(args));
	}
	catch(const std::exception& e){std::cerr << "ANDROID_LAUNCH_FAILED: " << e.what() << '\n';return 1;}
}

extern "C" JNIEXPORT jint JNICALL Java_org_runeclassic_port_RuneHostService_nativeRun(JNIEnv* env,jclass,jstring root,jstring run,jobjectArray arguments)
{
#ifdef OPENRUNE_ENABLE_NETWORK
    auto stringValue=[&](jstring value) { const char* p=env->GetStringUTFChars(value,nullptr);std::string out=p?p:"";if(p)env->ReleaseStringUTFChars(value,p);return out; };
    const auto files=stringValue(root),session=stringValue(run);
    int log=open((session+"/host.log").c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(log>=0){dup2(log,STDOUT_FILENO);dup2(log,STDERR_FILENO);close(log);}
    setvbuf(stdout,nullptr,_IOLBF,0);
    try {
        Directory::create(session+"/config/SurrealEngine");Directory::create(session+"/saves");
        setenv("SURREALENGINE_CONFIG_DIR",(session+"/config").c_str(),1);
        setenv("SURREALENGINE_RESOURCE_DIR",files.c_str(),1);
        Array<std::string> args;
        for(jsize i=0;i<env->GetArrayLength(arguments);++i){auto value=static_cast<jstring>(env->GetObjectArrayElement(arguments,i));args.push_back(stringValue(value));env->DeleteLocalRef(value);}
        GameApp app;const auto result=app.main(std::move(args));RuneMultiplayer::HostReady=false;return result;
    }catch(const std::exception& e){RuneMultiplayer::SetHostFailure(e.what());RuneMultiplayer::HostReady=false;return 1;}
#else
    RuneMultiplayer::SetHostFailure("Networking is not enabled in this build.");return 1;
#endif
}
extern "C" JNIEXPORT jint JNICALL Java_org_runeclassic_port_RuneBotService_nativeRun(JNIEnv* env,jclass type,jstring root,jstring run,jobjectArray arguments)
{
    return Java_org_runeclassic_port_RuneHostService_nativeRun(env,type,root,run,arguments);
}
extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_RuneHostService_nativeStop(JNIEnv*,jclass){RuneMultiplayer::HostStop=true;}
extern "C" JNIEXPORT jboolean JNICALL Java_org_runeclassic_port_RuneHostService_nativeReady(JNIEnv*,jclass){return RuneMultiplayer::HostReady.load();}
extern "C" JNIEXPORT jstring JNICALL Java_org_runeclassic_port_RuneHostService_nativeError(JNIEnv* env,jclass){return env->NewStringUTF(RuneMultiplayer::GetHostFailure().c_str());}

extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeControllerAxes(JNIEnv*, jclass, jfloat x, jfloat y, jfloat yaw, jfloat pitch)
{
    RuneController::Set({x,y,yaw,pitch});
}

extern "C" JNIEXPORT jstring JNICALL Java_org_runeclassic_port_MainActivity_nativeCharacterRoster(JNIEnv* env,jclass) {
    std::string roster;
    for(const auto& c:openrune::Characters)roster+=std::string(c.className)+"|"+c.name+"|"+std::to_string(c.skins)+"\n";
    return env->NewStringUTF(roster.c_str());
}

extern "C" JNIEXPORT void JNICALL Java_org_runeclassic_port_MainActivity_nativeCharacterPreviewRotation(JNIEnv*,jclass,jint character,jint skin,jfloat yaw) {
    RuneCharacterPreview::Rotate(character,skin,yaw);
}

extern "C" JNIEXPORT jint JNICALL Java_org_runeclassic_port_MainActivity_nativeCharacterPreview(JNIEnv* env,jclass,jint character,jint skin,jintArray destination,jfloat yaw) {
    if(!destination)return -1;
    int size=env->GetArrayLength(destination),small=RuneCharacterPreview::Width*RuneCharacterPreview::Height;
    if(size!=small && size!=small*4)return -1;
    std::vector<uint32_t> pixels;
    int status=RuneCharacterPreview::Poll(character,skin,pixels,size==small*4,yaw);if(status!=1)return status;
    env->SetIntArrayRegion(destination,0,jint(pixels.size()),reinterpret_cast<const jint*>(pixels.data()));
    return !env->ExceptionCheck();
}

extern "C" JNIEXPORT jint JNICALL Java_org_runeclassic_port_MainActivity_nativeBattleLaws(JNIEnv*,jclass){return RuneBattleLaws::Mask();}
