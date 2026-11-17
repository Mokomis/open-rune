#include "RuneDesktopMenu.h"
#include "RuneSnow.h"
#include "RuneController.h"
#include "RuneVisualEffects.h"
#include "RuneStairPresentation.h"
#include "RuneSlamVisualProbe.h"

#include "Precomp.h"
#ifdef __ANDROID__
#include <android/trace.h>
#endif
#include "RuneBreath.h"
#include "RuneShieldTaunt.h"
#include "RuneLeg.h"
#include "RuneSpider.h"
#include "RunePhysics.h"
#include "RuneNavigation.h"
#include "RuneGore.h"
#include "RuneGoreProbe.h"
#include "RuneCorpseVisualProbe.h"
#include "RunePhysicsVisualProbe.h"
#include "RuneLocalStairProbe.h"
#include <iostream>
#include "Engine.h"
#include "RuneMenu.h"
#include "RuneCharacters.h"
#include "RuneCharacterPreview.h"
#include "RuneView.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "RuneBloodlust.h"
#include "RuneLiftProbe.h"
#ifdef OPENRUNE_ENABLE_NETWORK
#include "RuneMultiplayer.h"
#endif
#ifdef __ANDROID__
#include <surrealgpu/androidframepacingcontrol.h>
#endif
#include "Utils/File.h"
#include "Utils/StrTools.h"
#include "Utils/SHA1Sum.h"
#include "Render/RenderSubsystem.h"
#include "Package/PackageManager.h"
#include "Package/IniProperty.h"
#include "Package/ObjectStream.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UFunction.h"
#include "Packages/Core/USubsystem.h"
#include "Packages/Core/Properties/UObjectProperty.h"
#include "Packages/Core/Properties/UStringProperty.h"
#include "Packages/Core/Properties/UFloatProperty.h"
#include "Packages/Core/Properties/UIntProperty.h"
#include "Packages/Engine/UClient.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/UCanvas.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/UPlayer.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/Info/UPlayerReplicationInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/UFont.h"
#include "Packages/Engine/Resources/Mesh/USkeletalMesh.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Resources/UMusic.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Engine/USurrealClient.h"
#include "Packages/Engine/Subsystems/UGameEngine.h"
#include "Packages/Engine/Subsystems/USurrealRenderDevice.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Packages/Engine/Subsystems/USurrealNetworkDevice.h"
#include "Packages/Extension/UPlayerPawnExt.h"
#include "Packages/Extension/Flags/UFlag.h"
#include "Packages/Extension/Flags/UFlagBase.h"
#include "Packages/Extension/Windows/UGC.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"
#include "Packages/ConSys/UConItem.h"
#include "Packages/ConSys/UConversation.h"
#include "Packages/ConSys/UConversationList.h"
#include "Packages/ConSys/UConversationMissionList.h"
#include "Packages/ConSys/Events/UConEvent.h"
#include "Packages/ConSys/Events/UConEventTransferObject.h"
#include "Packages/ConSys/Events/UConEventCheckObject.h"
#include "Packages/DeusEx/UDeusExLevelInfo.h"
#include "Packages/DeusEx/UDeusExSaveInfo.h"
#include "ObjectTravelInfo.h"
#include "Math/quaternion.h"
#include "Math/FrustumPlanes.h"
#include "GameWindow.h"
#include "RenderDevice/RenderDevice.h"
#include "RenderDevice/VisualComparison.h"
#include "RuneCombat.h"
#include "RuneCombatProbe.h"
#include "RuneSaveProbe.h"
#include "RuneRopeProbe.h"
#include "RuneGibProbe.h"
#include "RuneKickProbe.h"
#include "RuneBashProbe.h"
#include "RuneFeedbackProbe.h"
#include "RuneParticleProfile.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include "Video/VideoPlayer.h"
#include "Utils/Convert.h"
#include "Utils/CommandLine.h"
#include <fstream>
#include <chrono>
#include <thread>
#include <set>

Engine* engine = nullptr;

Engine::Engine(GameLaunchInfo launchinfo, bool packageProbe) : LaunchInfo(launchinfo)
{
	engine = this;

	packages = std::make_unique<PackageManager>(LaunchInfo);
	if (packageProbe)
		return;

	int probeSeed = commandline->GetArgInt("", "--probe-seed", -1);
	std::srand(probeSeed >= 0 ? (unsigned int)probeSeed : (unsigned int)std::time(nullptr));

	auto transientpkg = packages->GetTransientPackage();
	auto enginepkg = packages->GetPackage("Engine");
	gameengine = UObject::Cast<UGameEngine>(transientpkg->NewObject("gameengine", enginepkg->GetClass("GameEngine"), ObjectFlags::Transient));
	audiodev = UObject::Cast<USurrealAudioDevice>(transientpkg->NewObject("audiodev", enginepkg->GetClass("SurrealAudioDevice"), ObjectFlags::Transient));
	renderdev = UObject::Cast<USurrealRenderDevice>(transientpkg->NewObject("renderdev", enginepkg->GetClass("SurrealRenderDevice"), ObjectFlags::Transient));
	netdev = UObject::Cast<USurrealNetworkDevice>(transientpkg->NewObject("netdev", enginepkg->GetClass("SurrealNetworkDevice"), ObjectFlags::Transient));
	client = UObject::Cast<USurrealClient>(transientpkg->NewObject("client", enginepkg->GetClass("SurrealClient"), ObjectFlags::Transient));
	viewport = UObject::Cast<UViewport>(transientpkg->NewObject("viewport", enginepkg->GetClass("Viewport"), ObjectFlags::Transient));
	canvas = UObject::Cast<UCanvas>(transientpkg->NewObject("canvas", enginepkg->GetClass("Canvas"), ObjectFlags::Transient));
	DefaultTexture = UObject::Cast<UTexture>(packages->GetPackage("Engine")->GetUObject("Texture", "DefaultTexture"));

	floatprop = GC::Alloc<UFloatProperty>(NameString(), nullptr, ObjectFlags::NoFlags);

	if (LaunchInfo.IsDeusEx())
	{
		auto extpkg = packages->GetPackage("Extension");
		deusExPackage = packages->GetPackage("DeusEx");
		dxgc = UObject::Cast<UGC>(transientpkg->NewObject("gc", extpkg->GetClass("GC"), ObjectFlags::Transient));
		dxgc->Canvas() = canvas;
		dxSaveInfo = UObject::Cast<UDXSaveInfo>(transientpkg->NewObject("DeusExSaveInfo", deusExPackage->GetClass("DeusExSaveInfo"), ObjectFlags::Transient));
		dxConMissionList = UObject::Cast<UConversationMissionList>(packages->GetPackage("DeusExConText")->GetUObject("ConversationMissionList", "ConMissionList"));
	}

	std::string consolestr = packages->GetIniValue("system", "Engine.Engine", "Console");
	std::string consolepkg = consolestr.substr(0, consolestr.find('.'));
	std::string consolecls = consolestr.substr(consolestr.find('.') + 1);
	console = UObject::Cast<UConsole>(transientpkg->NewObject("console", packages->GetPackage(consolepkg)->GetClass(consolecls), ObjectFlags::Transient));

	console->Viewport() = viewport;
	canvas->Viewport() = viewport;
	viewport->Console() = console;
}

Engine::~Engine()
{
    RuneDesktopMenu::Stop(*this);
	if (!gameengine) // The package-only probe has no game state or log destination.
	{
		engine = nullptr;
		return;
	}
	if (audiodev)
		audiodev->ShutdownDevice();

	// Saving diagnostics must not throw from this noexcept destructor. A fresh
	// headless installation may not have created the settings directory yet.
	if (!NetworkAuthority) try
	{
		auto path = Directory::localAppData() / "SurrealEngine/SE-Log-LastRun.txt";
		Directory::create(path.parent_path().string());
		Logger::Get()->SaveLogAsPlaintext(path.string());
	}
	catch (const std::exception& error)
	{
		std::cerr << "Open Rune could not save the shutdown log: " << error.what() << '\n';
	}

	engine = nullptr;
}

void Engine::Run()
{
	LogMessage("Game: " + LaunchInfo.gameName + " (Version: " + LaunchInfo.gameVersionString + ")");
	LoadEngineSettings();
	LogMessage("Loaded Engine settings");
	LoadKeybindings();
	LogMessage("Loaded key bindings");
	LogGamePackageSHA1Sums();

    // Rune's configured default is separate from SurrealEngine's MainFOV
    // override on the live pawn. Use the authored config as the zoom baseline
    // without rewriting script-owned camera fields or old saved actors.
    const float runeAuthoredFov=LaunchInfo.IsRune() ? IniPropertyConverter<float>::FromIniFile(
        *packages->GetIniFile("user"),"Engine.PlayerPawn","DefaultFOV",75.0f) : 90.0f;

	OpenWindow();

	audiodev->InitDevice();
	render = std::make_unique<RenderSubsystem>(window->GetRenderDevice());

	if (engine->LaunchInfo.ue1Version > 219 && !client->StartupFullscreen)
		viewport->bWindowsMouseAvailable() = true;

	window->LockCursor();

	if (packages->IsKlingonHonorGuard())
	{
		PlayAVI({ "playavi", "INTRO.AVI", "N" });
	}

	if (!LaunchInfo.noEntryMap)
		LoadEntryMap();

	if (LaunchInfo.url.empty())
		LoadMap(GetDefaultURL(packages->GetIniValue("system", "URL", "LocalMap")));
	else
		LoadMap(UnrealURL(GetDefaultURL(packages->GetIniValue("system", "URL", "LocalMap")), LaunchInfo.url));

	LoginPlayer();

	auto objprop = GC::Alloc<UObjectProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
	auto vecprop = GC::Alloc<UStructProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
	auto rotprop = GC::Alloc<UStructProperty>(NameString(), nullptr, ObjectFlags::NoFlags);

	bool firstCall = true;
    if(commandline->HasArg("", "--rune-effects"))RuneVisualEffects::Enabled.store(commandline->GetArgInt("", "--rune-effects",1)!=0);
    if(commandline->HasArg("","--snow"))RuneSnow::Mode=RuneSnow::ClampMode(commandline->GetArgInt("","--snow",-1));
    if(commandline->HasArg("","--gore"))RuneGore::Enabled=commandline->GetArgInt("","--gore",1)!=0;
    LogMessage("RUNE_EFFECTS_MODE enabled="+std::to_string(RuneVisualEffects::Enabled.load()));
	if(commandline->HasArg("", "--rune-tactics"))RuneTactics::Enabled.store(commandline->GetArgInt("", "--rune-tactics", 1)!=0);
	int remainingFrames = commandline->GetArgInt("", "--maxframes", 0);
    if(commandline->HasArg("","--snow-weather-test")) {
        if(remainingFrames<=0 || !commandline->HasArg("","--save-directory"))Exception::Throw("Weather test requires bounded frames and isolated saves");
        RuneSnow::TestSeed=uint32_t(std::max(1,commandline->GetArgInt("","--snow-weather-seed",1)));
        RuneSnow::TestPeriodMs=6000;RuneSnow::BeginMap(TotalTime,false);RuneSnow::Mode=RuneSnow::Automatic;
    }
    const int probeTickHz=commandline->GetArgInt("", "--probe-tick-hz", 0);
    if(probeTickHz && (probeTickHz<10 || probeTickHz>1000 || remainingFrames<=0 ||
        !commandline->HasArg("", "--save-directory")))
        Exception::Throw("Fixed-step rendering probe requires 10-1000 Hz, bounded frames and isolated saves");
    if(probeTickHz) LogMessage("RUNE_FIXED_STEP_PROBE hz="+std::to_string(probeTickHz));
	int forwardProbeFrames = commandline->GetArgInt("", "--probe-forward-frames", 0);
	vec3 forwardProbeStart(0.0f);
	int fireProbeFrame=commandline->GetArgInt("", "--probe-fire-frame", 0);
	int probeFrame=0;
    RuneLiftProbe liftProbe;
    RuneGoreProbe goreProbe;
    RuneCorpseVisualProbe corpseProbe;
    RunePhysicsVisualProbe physicsProbe;
    const auto normalSaveFolder=packages->GetSaveFolderPath();bool physicsSaveScope=false;
    RuneLocalStairProbe localStairProbe;
    RuneSlamVisualProbe slamProbe;
	int profileFrames=commandline->GetArgInt("", "--profile-frames", 0);
#ifdef __ANDROID__
    const bool traceFrame = commandline->HasArg("", "--frame-benchmark") && profileFrames > 0;
#endif
	RuneParticleProfile::Milliseconds = {};
	std::vector<double> profileTimes;
	double profileTick=0,profileRender=0,profilePacing=0;
	int maxFps = clamp(commandline->GetArgInt("", "--maxfps", 0), 0, 1000);
	auto frameInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(maxFps ? 1.0/maxFps : 0.0));
	auto nextFrame = std::chrono::steady_clock::now();
	std::string targetClass=commandline->GetArg("", "--probe-target", "");
	UActor* combatProbeTarget=nullptr;
	std::string enemyClass=commandline->GetArg("", "--probe-enemy", "");
	std::string shieldClass=commandline->GetArg("", "--probe-shield", "");
	int enemySpawnFrame=commandline->GetArgInt("", "--probe-enemy-frame", 30);
	// A practice fight has no campaign checkpoint. Keep its recipe, not actor
	// pointers, so death recovery can recreate the same map and opponent.
	auto practiceURL=LevelInfo->URL;
	std::string practiceEnemy=enemyClass,practiceShield=shieldClass;
	int practiceSpawnFrame=enemySpawnFrame;
	RuneCombat::PracticeArena.store(!practiceEnemy.empty());
	UPawn* combatEnemy=nullptr;
	int combatStartFrame=0;
	RuneCombatProbe advancedCombatProbe;
	RuneSaveProbe saveProbe;
	RuneRopeProbe ropeProbe;
	RuneGibProbe gibProbe;
	RuneKickProbe kickProbe;
    RuneBashProbe bashProbe;
    RuneFeedbackProbe feedbackProbe;
	bool sessionSavePending=false;
	int sessionSaveSlot=9;
	bool menuWasOpen=false, coveredWasOpen=false;
	bool titleIdle=commandline->HasArg("", "--menu-title");
    #if defined(_WIN32) && defined(OPENRUNE_ENABLE_NETWORK)
    const auto desktopArgs=std::wstring(GetCommandLineW());
    const bool desktopDiagnostic=desktopArgs.find(L"--probe")!=std::wstring::npos || desktopArgs.find(L"--rune-")!=std::wstring::npos || commandline->HasArg("","--maxframes") || commandline->HasArg("","--profile-frames") || commandline->HasArg("","--capture-frame");
    const bool desktopRequested=commandline->HasArg("","--desktop-menu");
    if(desktopRequested || (!desktopDiagnostic && !commandline->HasArg("","--no-desktop-menu"))) {
        if(!desktopRequested)titleIdle=true;
        RuneDesktopMenu::Start(*this,titleIdle);
    }
#endif
	// Publish the save directory on the first frame, before Combat::Ready is
	// exposed to the menu. Covered title screens run at 10 Hz, so waiting sixty
	// frames allowed an enabled Load page to cache six seconds of empty metadata.
	int menuMetadataFrame=59;
	int menuTargetFps=120;
	bool menuPacing=true;
	int fireHoldFrames=commandline->GetArgInt("", "--probe-fire-hold", 60);
	int fireRepeat=commandline->GetArgInt("", "--probe-fire-repeat", 0);
	int blockFrame=commandline->GetArgInt("", "--probe-block-frame", 0);
	int blockHold=commandline->GetArgInt("", "--probe-block-hold", 600);
	bool enhanced = commandline->GetArgInt("", "--visual-enhanced", 0) != 0;
	int appliedVisualMode = -1;
	int clarity = commandline->GetArgInt("", "--clarity", 0);
	int appliedClarity = -1;
	int worldFeatures = commandline->GetArgInt("", "--world-features", 0) & 262143;
    int texturePackQuality = clamp(commandline->GetArgInt("", "--texture-pack", 0), 0, 2);
	int upscaleMode = clamp(commandline->GetArgInt("", "--upscale", 0), 0, 3);
	int appliedUpscale = -1;
	const float originalScale = window->GetRenderDevice()->ResolutionScale;
    const bool materialCycle=commandline->HasArg("","--probe-material-cycle");
    if(materialCycle && (remainingFrames<=0 || !commandline->HasArg("","--save-directory")))
        Exception::Throw("Material comparison requires bounded frames and isolated saves");
    std::vector<double> materialTimes;
    const bool snowCycle=commandline->HasArg("","--probe-snow-cycle");
    if(snowCycle && (remainingFrames<=0 || !commandline->HasArg("","--save-directory")))
        Exception::Throw("Snow comparison requires bounded frames and isolated saves");
    std::vector<double> snowTimes;
    if(materialCycle) {
        // Account for the one-time bake separately from steady frame timing.
        auto* device=window->GetRenderDevice();int saved=device->WorldFeatures;
        device->WorldFeatures=worldFeatures|196608;
        Level->Light.PrepareEnvironmentProbes();
        device->WorldFeatures=saved;
    }
	int visualFpsFrames = 0;
	auto visualFpsStart = std::chrono::steady_clock::now();

	float currentZoneTimeDilation = 1.0f; // Unreal 227 allows zones to specify their own time dilations

	while (!quit)
	{
#ifdef OPENRUNE_ENABLE_NETWORK
        if(auto invite=RuneMultiplayer::TakeInvite();!invite.empty())
        {
            if(physicsSaveScope){packages->SetSaveFolderPath(normalSaveFolder);physicsSaveScope=false;physicsProbe.Stop();}
            RuneCombat::Ready=false;RuneCombat::SessionRequest=0;RuneCombat::PracticeArena=false;
            SaveGameInfo.SaveGameSlot=DONT_SAVE_GAME;sessionSavePending=false;
            combatEnemy=nullptr;combatProbeTarget=nullptr;
            activeInputButtons.clear();activeInputAxes.clear();runePendingButtons.clear();runePulsedButtons.clear();MouseMoveX=MouseMoveY=0;
            targetClass.clear();enemyClass.clear();shieldClass.clear();
            fireProbeFrame=blockFrame=forwardProbeFrames=0;
            try { RunRuneMultiplayerClient(*this,invite); }
            catch(const std::exception& error) { RuneMultiplayer::SetStatus(error.what());LogMessage(error.what()); }
            if(quit)break;
            NetworkClient=false;RuneMultiplayer::Active=false;RuneMenu::PlayerDead=false;
            SaveGameInfo.SaveGameSlot=DONT_SAVE_GAME;ClientTravelInfo={};
            LoadMap(GetDefaultURL("DM-Hildir.run"));LoginPlayer();CallEvent(console,"CloseUWindow");
            titleIdle=true;RuneMenu::Open=true;RuneCombat::Ready=true;CalcTimeElapsed();
            // Settings changed in the match remain active in the campaign.
            enhanced=window->GetRenderDevice()->VisualEnhanced;
            clarity=VisualComparison::AppliedClarity.load();worldFeatures=window->GetRenderDevice()->WorldFeatures;
            texturePackQuality=window->GetRenderDevice()->TexturePackQuality;
            upscaleMode=VisualComparison::AppliedUpscale.load();
            continue;
        }
#endif
		RuneParticleProfile::Enabled = profileFrames > 0 && probeFrame >= 60 && probeFrame < 60 + profileFrames;
		int sessionAction=RuneCombat::SessionRequest.exchange(0);
		if (sessionAction) RuneCombat::Ready.store(false);
        if(physicsSaveScope&&(sessionAction==1||sessionAction==3||sessionAction==4||sessionAction==7||sessionAction==5)){packages->SetSaveFolderPath(normalSaveFolder);physicsSaveScope=false;physicsProbe.Stop();}
        if(sessionAction==9){packages->SetSaveFolderPath(normalSaveFolder.parent_path()/"PhysicsPlayground");physicsSaveScope=true;physicsProbe.Start(probeFrame);RunePhysics::RequestedMode=std::max(1,RunePhysics::RequestedMode.load());sessionAction=5;}

		if (sessionAction>0 && sessionAction<=7 && sessionAction!=5 && LaunchInfo.IsRune() && viewport->Actor())
		{
			if (sessionAction==2 || sessionAction==6)
			{
				sessionSaveSlot=sessionAction==6 ? clamp(RuneMenu::Slot.load(),0,8) : 9;
				CallEvent(viewport->Actor(), "QuickSave");
				sessionSavePending=SaveGameInfo.SaveGameSlot==9;
				if (!sessionSavePending) RuneCombat::SessionResult.store(4);
			}
			else
			{
				int slot=sessionAction==7 ? clamp(RuneMenu::Slot.load(),0,10) : sessionAction==4?10:9;
				if (sessionAction!=1 && !fs::is_regular_file(packages->GetSaveFolderPath()/("Save"+std::to_string(slot)+"."+packages->GetSaveExtension())))
					RuneCombat::SessionResult.store(3);
				else
				{
					titleIdle=false;
                    SaveGameInfo.SaveGameSlot=DONT_SAVE_GAME;
                    SaveGameInfo.SaveGameDescription.clear();
                    combatEnemy=nullptr; combatProbeTarget=nullptr;
					activeInputButtons.clear(); activeInputAxes.clear();
					runePendingButtons.clear();runePulsedButtons.clear();
					targetClass.clear(); enemyClass.clear(); shieldClass.clear();
					RuneCombat::PracticeArena=false;
					fireProbeFrame=blockFrame=forwardProbeFrames=0;
					ClientTravelInfo.TravelType=ETravelType::TRAVEL_Absolute;
					ClientTravelInfo.TransferItems=false;
					if (sessionAction==1)
					{
						auto url=GetDefaultURL("RagnarVillage.run");
						url.AddOrReplaceOption("Difficulty="+std::to_string(RuneMenu::Difficulty.load()));
						LoadMap(url); LoginPlayer();
					}
					else { LoadFromSaveFile(UnrealURL("?load="+std::to_string(slot))); PossessSavedPlayer(); }
					CallEvent(console,"CloseUWindow");
					CalcTimeElapsed();
					RuneCombat::SessionResult.store(sessionAction==1?5:2);
					LogMessage("RUNE_SESSION_ACTION action="+std::to_string(sessionAction)+" map="+LevelInfo->URL.Map);
				}
			}
		}
		if ((sessionAction==5 || (sessionAction==8 && RuneCombat::PracticeArena.load())) && LaunchInfo.IsRune())
		{
            if(sessionAction==5) {
                practiceURL=GetDefaultURL("DM-Hildir.run");
                practiceEnemy=physicsSaveScope?"":"GoblinWarrior";practiceShield=physicsSaveScope?"":"VikingShield";practiceSpawnFrame=240;
            }
            titleIdle=false;
            SaveGameInfo.SaveGameSlot=DONT_SAVE_GAME;
            SaveGameInfo.SaveGameDescription.clear();
			// A fresh arena is deliberately separate from campaign saves. Drop
			// references and held inputs before unloading the previous encounter.
			combatEnemy=nullptr;
			combatProbeTarget=nullptr;
			activeInputButtons.clear();
			runePendingButtons.clear();runePulsedButtons.clear();
			activeInputAxes.clear();
			targetClass.clear();
			fireProbeFrame=blockFrame=forwardProbeFrames=0;
			ClientTravelInfo.TravelType=ETravelType::TRAVEL_Absolute;
			ClientTravelInfo.TransferItems=false;
            if(physicsSaveScope)physicsProbe.Start(probeFrame);
			LoadMap(practiceURL);
			LoginPlayer();
			CallEvent(console,"CloseUWindow");
			combatStartFrame=probeFrame;
			enemyClass=practiceEnemy;
			shieldClass=practiceShield;
			enemySpawnFrame=practiceSpawnFrame;
			RuneCombat::PracticeArena=true;
			CalcTimeElapsed(); // Do not advance the fight by the map loading time.
			LogMessage(std::string(sessionAction==8?"RUNE_RETRY_FIGHT":"RUNE_NEW_FIGHT")+" arena="+practiceURL.Map+" enemy="+enemyClass);
		}
		int requestedUpscale = VisualComparison::RequestedUpscale.exchange(-1);
		if (requestedUpscale >= 0) upscaleMode = clamp(requestedUpscale, 0, 3);
		window->GetRenderDevice()->UpscaleSGSR = upscaleMode != 0;
		window->GetRenderDevice()->ResolutionScale = upscaleMode == 3 ? 0.25f : upscaleMode == 2 ? 0.5f : upscaleMode == 1 ? 0.75f : originalScale;
		if (appliedUpscale != upscaleMode)
		{
			appliedUpscale = upscaleMode;
			visualFpsFrames = 0;
			visualFpsStart = std::chrono::steady_clock::now();
			VisualComparison::EngineFps.store(0.0f);
			LogMessage(std::string("RUNE_UPSCALE mode=") + (upscaleMode == 3 ? "sgsr25" : upscaleMode == 2 ? "sgsr50" : upscaleMode == 1 ? "sgsr75" : "native") + " scale=" + std::to_string(window->GetRenderDevice()->ResolutionScale));
		}
		int requestedVisualMode = VisualComparison::RequestedMode.exchange(-1);
        int requestedTexturePack = VisualComparison::RequestedTexturePack.exchange(-1);
        if (requestedTexturePack >= 0) texturePackQuality = clamp(requestedTexturePack, 0, 2);
        if (window->GetRenderDevice()->TexturePackQuality != texturePackQuality)
        {
            window->GetRenderDevice()->Flush(false); // Drains GPU before releasing cached images/descriptors.
            window->GetRenderDevice()->TexturePackQuality = texturePackQuality;
            LogMessage("RUNE_TEXTURE_PACK quality=" + std::to_string(texturePackQuality));
        }
        VisualComparison::AppliedTexturePack.store(texturePackQuality);
		int requestedWorld = VisualComparison::RequestedWorldFeatures.exchange(-1);
		if (requestedWorld >= 0) worldFeatures = requestedWorld & 262143;
        if(materialCycle) {
            // Alternate within one process to reduce clock/temperature and
            // launch-to-launch variance. Skip initial warmup and switch frames.
            const int modes[]={0,196608,196608,0};
            worldFeatures=(worldFeatures&~196608)|modes[(probeFrame/240)%4];
        }
        if(snowCycle) {
            const int modes[]={0,2,3,3,2,0};
            RuneSnow::Mode=probeFrame<600?3:modes[((probeFrame-600)/600)%6];
        }
		window->GetRenderDevice()->WorldFeatures = worldFeatures;
		if (VisualComparison::AppliedWorldFeatures.exchange(worldFeatures) != worldFeatures)
			LogMessage("RUNE_WORLD_FEATURES code=" + std::to_string(worldFeatures));
		int requestedClarity = VisualComparison::RequestedClarity.exchange(-1);
		if (requestedClarity >= 0) clarity = requestedClarity;
		// Clamp each field independently; 0 remains the exact pre-clarity path.
		int sharpLevel = clamp(clarity & 3, 0, 3);
		int shadowLevel = clamp((clarity >> 3) & 3, 0, 3);
		clarity = sharpLevel | (clarity & 4) | (shadowLevel << 3);
		auto clarityRenderer = window->GetRenderDevice();
		clarityRenderer->AdaptiveSharpness = sharpLevel == 1 ? 0.35f : sharpLevel == 2 ? 0.65f : sharpLevel == 3 ? 0.90f : 0.0f;
		clarityRenderer->ShadowLift = shadowLevel == 1 ? 0.35f : shadowLevel == 2 ? 0.65f : shadowLevel == 3 ? 0.90f : 0.0f;
		clarityRenderer->TextureAnisotropy = (clarity & 4) ? 16.0f : 8.0f;
		if (appliedClarity != clarity)
		{
			appliedClarity = clarity;
			visualFpsFrames = 0;
			visualFpsStart = std::chrono::steady_clock::now();
			VisualComparison::EngineFps.store(0.0f);
			LogMessage("RUNE_CLARITY code=" + std::to_string(clarity) + " sharp=" + std::to_string(sharpLevel) + " shadows=" + std::to_string(shadowLevel) + " anisotropy=" + std::to_string(clarityRenderer->TextureAnisotropy));
		}
		if (requestedVisualMode >= 0) enhanced = requestedVisualMode != 0;
		window->GetRenderDevice()->VisualEnhanced = enhanced;
		if (appliedVisualMode != (int)enhanced)
		{
			appliedVisualMode = (int)enhanced;
			visualFpsFrames = 0;
			visualFpsStart = std::chrono::steady_clock::now();
			VisualComparison::EngineFps.store(0.0f);
			LogMessage(std::string("RUNE_VISUAL mode=") + (enhanced ? "enhanced" : "original"));
		}
        int requestedAA=RuneMenu::Antialias.exchange(-1);
        if(requestedAA>=0) {
            window->GetRenderDevice()->AntialiasMode=clamp(requestedAA,0,2);
            LogMessage("RUNE_MENU msaa="+std::to_string(requestedAA));
        }
        int requestedTarget=RuneMenu::TargetFps.exchange(-1);
        int requestedPacing=RuneMenu::Pacing.exchange(-1);
#ifdef __ANDROID__
        if(requestedTarget>=0 || requestedPacing>=0) {
            if(requestedTarget>=0)menuTargetFps=requestedTarget;
            if(requestedPacing>=0)menuPacing=requestedPacing!=0;
            AndroidFramePacingConfigure(requestedTarget,requestedPacing);
            // Swappy owns cadence when enabled. The off comparison uses the
            // same target through the existing CPU limiter, never both at once.
            maxFps=menuPacing ? clamp(commandline->GetArgInt("", "--maxfps", 0),0,1000) : menuTargetFps;
            frameInterval=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(maxFps?1.0/maxFps:0.0));
            nextFrame=std::chrono::steady_clock::now();
        }
#else
        if(RuneDesktopMenu::Enabled(*this) && (requestedTarget>=0 || requestedPacing>=0)) {
            if(requestedTarget>=0)menuTargetFps=std::clamp(requestedTarget,30,1000);
            if(requestedPacing>=0)menuPacing=requestedPacing!=0;
            maxFps=menuTargetFps;window->GetRenderDevice()->UseVSync=menuPacing;
            frameInterval=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0/maxFps));
            nextFrame=std::chrono::steady_clock::now();
        }
#endif
        bool menuOpen=titleIdle || RuneMenu::Open.load();
        if(menuOpen != menuWasOpen) {
            if(viewport->Actor()) {
                for(const auto& input:activeInputButtons) viewport->Actor()->SetBool(input.first,false);
                for(const auto& input:runePulsedButtons) viewport->Actor()->SetBool(input,false);
                for(const auto& input:activeInputAxes) viewport->Actor()->SetFloat(input.first,0.0f);
            }
            activeInputButtons.clear(); activeInputAxes.clear();
            runePendingButtons.clear();runePulsedButtons.clear();
            MouseMoveX=MouseMoveY=0;
            CallEvent(console,"CloseUWindow");
            if(viewport && viewport->Actor())viewport->Actor()->RuneBashTapTime=-1;
            menuWasOpen=menuOpen;
            LogMessage(std::string("RUNE_MENU paused=")+(menuOpen?"1":"0")+" level_time="+std::to_string(LevelInfo->TimeSeconds())
                +" x="+std::to_string(viewport->Actor()->Location().x)+" y="+std::to_string(viewport->Actor()->Location().y)+" z="+std::to_string(viewport->Actor()->Location().z));
        }
        if(++menuMetadataFrame>=60 || sessionAction || sessionSavePending) {
            menuMetadataFrame=0;
            std::lock_guard<std::mutex> lock(RuneMenu::MetadataMutex);
            RuneMenu::Metadata=LevelInfo->URL.Map+"\n"+packages->GetSaveFolderPath().string()+"\n"+packages->GetSaveExtension();
        }
        auto profileStart=std::chrono::steady_clock::now();
        const bool coveredMenu=menuOpen && RuneMenu::Covered.load() && !RuneDesktopMenu::Enabled(*this);
        if(coveredMenu!=coveredWasOpen) {
            nextFrame=std::chrono::steady_clock::now();coveredWasOpen=coveredMenu;
            LogMessage(std::string("RUNE_MENU covered=")+(coveredMenu?"1":"0"));
        }
        const auto effectiveInterval=coveredMenu?std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(0.1)):frameInterval;
		if (maxFps > 0 || coveredMenu)
		{
            if (coveredMenu) {
                // Native UI remains independent; wake promptly when it exposes
                // the game or an editor instead of imposing a 100 ms input stall.
                while (RuneMenu::Open.load() && RuneMenu::Covered.load()) {
                    // Preview animation may refresh while the covered world
                    // retains its existing 10 Hz render/simulation cadence.
                    RuneCharacterPreview::Tick(*this);
                    auto now=std::chrono::steady_clock::now();
                    if(now>=nextFrame)break;
                    std::this_thread::sleep_until(std::min(nextFrame,now+std::chrono::milliseconds(RuneCharacterPreview::Active()?2:10)));
                }
            } else std::this_thread::sleep_until(nextFrame);
			auto now = std::chrono::steady_clock::now();
			// Keep a stable cadence, but never burst through missed deadlines after
			// a pause, map load, or a frame that takes more than a full interval.
			nextFrame = now > nextFrame + effectiveInterval ? now + effectiveInterval : nextFrame + effectiveInterval;
		}
		auto profileGameStart=std::chrono::steady_clock::now();
#ifdef __ANDROID__
        if (traceFrame) ATrace_beginSection("Rune Simulation");
#endif
		// Main game loop should consist of these 4 steps:
		// Tick everything
		// Render the scene
		// Check if there is a request to save the game: save the game if that's the case
		// Check if there is a new map to load (next level, saved game etc.): load it if that's the case

		// Tick everything
		if (LaunchInfo.IsUnreal1_227() && viewport->Actor())
			currentZoneTimeDilation = viewport->Actor()->PlayerReplicationInfo()->PlayerZone()->ZoneTimeDilation();

		float realTimeElapsed = CalcTimeElapsed();
        if(probeTickHz) realTimeElapsed=1.0f/probeTickHz;
		float entryLevelElapsed = EntryLevel ? realTimeElapsed * clamp(EntryLevelInfo->TimeDilation() * currentZoneTimeDilation, 0.0025f, 25.0f) : 0.0f;
		float levelElapsed = realTimeElapsed * clamp(LevelInfo->TimeDilation() * currentZoneTimeDilation, 0.0025f, 25.0f);

		if(menuOpen) { entryLevelElapsed=0.0f; levelElapsed=0.0f; }
		TotalTime += realTimeElapsed;

		if (!menuOpen && EntryLevel && EntryLevel != Level)
			EntryLevelInfo->TimeSeconds() += entryLevelElapsed;
		if (!menuOpen && Level)
			LevelInfo->TimeSeconds() += levelElapsed;

		Logger::Get()->SetTimeSeconds(LevelInfo->TimeSeconds());

		// Update the time fields
		std::time_t now = std::time(nullptr);
		std::tm* timedesc = std::localtime(&now);

		LevelInfo->Year() = timedesc->tm_year;
		LevelInfo->Month() = timedesc->tm_mon;
		LevelInfo->Day() = timedesc->tm_mday;
		LevelInfo->DayOfWeek() = timedesc->tm_wday;
		LevelInfo->Hour() = timedesc->tm_hour;
		LevelInfo->Minute() = timedesc->tm_min;
		LevelInfo->Second() = timedesc->tm_sec;
		LevelInfo->Millisecond() = 0; // No timedesc equivalent for LevelInfo->Millisecond()

		// To do: set these to true if the frame rate is too low
		if (LaunchInfo.ue1Version >= 436)
		{
			LevelInfo->bDropDetail() = false;
			LevelInfo->bAggressiveLOD() = false;
		}

		UpdateInput(realTimeElapsed);

		SetPause(menuOpen || !LevelInfo->Pauser().empty());

		// Do NOT pause this Tick event otherwise some messages will stay on screen forever.
		CallEvent(console, EventName::Tick, { ExpressionValue::FloatValue(levelElapsed) });

		if (EntryLevel && EntryLevel != Level)
			EntryLevel->Tick(entryLevelElapsed, m_GamePaused);
		Level->Tick(levelElapsed, m_GamePaused);

        // Publish independently of the HUD: its scripted meters disappear on
        // death, and the Original HUD option must still allow recovery.
        auto* menuPlayer = viewport->Actor();
        bool playerDead = LaunchInfo.IsRune() && !titleIdle && menuPlayer &&
            menuPlayer->Health() <= 0 && menuPlayer->GetStateName() == "Dying";
        if (RuneMenu::PlayerDead.exchange(playerDead) != playerDead)
            LogMessage(std::string("RUNE_PLAYER_DEAD value=") + (playerDead ? "1" : "0"));

		if (dxRootWindow)
			dxRootWindow->Tick(levelElapsed); // Should this maybe be realTimeElapsed?

		// To do: improve CallEvent so parameter passing isn't this painful
		UFunction* funcPlayerCalcView = viewport->Actor() ? FindEventFunction(viewport->Actor(), "PlayerCalcView") : nullptr;
		if (funcPlayerCalcView)
		{
			vecprop->Struct = UObject::Cast<UStructProperty>(funcPlayerCalcView->Properties[1])->Struct;
			rotprop->Struct = UObject::Cast<UStructProperty>(funcPlayerCalcView->Properties[2])->Struct;
			CameraActor = viewport->Actor();
			CameraLocation = viewport->Actor()->Location();
			CameraRotation = viewport->Actor()->Rotation();
			CameraFovAngle = viewport->Actor()->FovAngle();
			CallEvent(viewport->Actor(), EventName::PlayerCalcView, {
				ExpressionValue::Variable(&CameraActor, objprop),
				ExpressionValue::Variable(&CameraLocation, vecprop),
				ExpressionValue::Variable(&CameraRotation, rotprop)
				});
            if(LaunchInfo.IsRune()) {
                auto* player=viewport->Actor();
                auto* zone=player->Region().Zone;
                bool scripted=player->ViewTarget()!=nullptr || CameraActor!=player || player->bFixedCamera() ||
                    player->GetStateName()=="Scripting" ||
                    (zone && zone->HasProperty("bTakeOverCamera") && zone->GetBool("bTakeOverCamera"));
                float aspect=float(viewport->ViewportWidth())/std::max(viewport->ViewportHeight(),1);
                CameraFovAngle=RuneView::Adjust(player->FovAngle(),runeAuthoredFov,aspect,
                    RuneView::FieldOfView.load(),RuneView::AutomaticAspect.load(),scripted);
                RuneView::AppliedFov.store(CameraFovAngle);
                if(commandline->HasArg("", "--probe-view") && probeFrame==60)
                    LogMessage("RUNE_CAMERA authored="+std::to_string(player->FovAngle())+" default="+std::to_string(runeAuthoredFov)+
                        " desired="+std::to_string(player->DesiredFOV())+" aspect="+std::to_string(aspect)+" applied="+std::to_string(CameraFovAngle));
            }
		}

        // Isolated renderer comparisons: keep an exact view while simulation
        // (including moving water and reflected actors) continues normally.
        if(commandline->HasArg("", "--probe-view"))
        {
            if(!commandline->HasArg("", "--save-directory"))
                Exception::Throw("Fixed camera probe requires isolated saves");
            std::string value=commandline->GetArg("", "--probe-view", "");
            std::replace(value.begin(),value.end(),',',' ');
            std::istringstream input(value);
            vec3 position; int yaw=0,pitch=0; std::string trailing;
            if(!(input>>position.x>>position.y>>position.z>>yaw>>pitch) || (input>>trailing) ||
                !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
                yaw < -65536 || yaw > 65536 || pitch < -16384 || pitch > 16384)
                Exception::Throw("Invalid fixed camera probe: expected x,y,z,yaw,pitch");
            CameraLocation=position;
            CameraRotation=Rotator(pitch,yaw,0);
            if(probeFrame==0)LogMessage("RUNE_FIXED_VIEW value="+value);
        }

		UpdateAudio();

		viewport->SetViewportRect(0, 0, engine->window->GetPixelWidth(), engine->window->GetPixelHeight());
        liftProbe.Tick(*this,probeFrame);
        goreProbe.Before(*this,probeFrame);
        corpseProbe.Before(*this,probeFrame);
        if(auto* p=viewport->Actor())CameraLocation+=RunePhysics::RenderOffset(p);
        physicsProbe.Before(*this,probeFrame);
        RuneBreath::Demo(*this,probeFrame);
        RuneShieldTaunt::Demo(*this,probeFrame);
        RuneLeg::Demo(*this,probeFrame);
        RuneSpider::Demo(*this,probeFrame);
        localStairProbe.Before(*this,probeFrame);
        slamProbe.Before(*this,probeFrame);
        RuneStairPresentation::Update(viewport->Actor(),m_GamePaused?0.f:levelElapsed);

		auto profileDrawStart=std::chrono::steady_clock::now();
#ifdef __ANDROID__
        if (traceFrame) {
            ATrace_endSection();
            ATrace_beginSection("Rune Render");
        }
#endif
        {RunePhysics::PresentationScope presentation;
		render->DrawGame(m_GamePaused?0.f:levelElapsed);
        }
		auto profileDrawEnd=std::chrono::steady_clock::now();
#ifdef __ANDROID__
        if (traceFrame) ATrace_endSection();
#endif
        if(commandline->HasArg("","--snow-weather-captures") && commandline->HasArg("","--snow-weather-test"))
            for(int capture:{90,270,450,630,810,990})if(probeFrame==capture){
                auto* device=window->GetRenderDevice();int w=device->GetRenderWidth(),h=device->GetRenderHeight();Array<TextureColor> pixels;pixels.resize(size_t(w)*h);
                auto saved=device->GammaCorrectScreenshots;device->GammaCorrectScreenshots=true;device->ReadPixels(pixels.data());device->GammaCorrectScreenshots=saved;
                std::ofstream image(commandline->GetArg("","--snow-weather-captures","")+"-"+std::to_string(capture)+".ppm",std::ios::binary);image<<"P6\n"<<w<<" "<<h<<"\n255\n";for(auto c:pixels){image.put(char(c.R));image.put(char(c.G));image.put(char(c.B));}
            }

        goreProbe.After(probeFrame);
        corpseProbe.After(*this,probeFrame);
        physicsProbe.After(*this,probeFrame);
        localStairProbe.After(*this,probeFrame);
        slamProbe.After(*this,probeFrame);
		VisualComparison::AppliedMode.store(appliedVisualMode);
		VisualComparison::AppliedUpscale.store(appliedUpscale);
		VisualComparison::AppliedClarity.store(appliedClarity);
		RuneCombat::Ready.store(LaunchInfo.IsRune() && RuneCombat::SessionRequest.load()==0);
		++visualFpsFrames;
		double visualSeconds = std::chrono::duration<double>(profileDrawEnd - visualFpsStart).count();
		if (visualSeconds >= 1.0)
		{
			VisualComparison::EngineFps.store((float)(visualFpsFrames / visualSeconds));
			visualFpsFrames = 0;
			visualFpsStart = profileDrawEnd;
		}
        if(materialCycle) {
            int local=probeFrame%240;
            if(local==0)materialTimes.clear();
            if(local>=60)materialTimes.push_back(std::chrono::duration<double,std::milli>(profileDrawEnd-profileGameStart).count());
            if(local==239) {
                double sum=0;for(double ms:materialTimes)sum+=ms;
                std::sort(materialTimes.begin(),materialTimes.end());
                LogMessage("RUNE_MATERIAL_PERFORMANCE block="+std::to_string(probeFrame/240)+" enabled="+
                    std::to_string((worldFeatures&196608)!=0)+" frames="+std::to_string(materialTimes.size())+
                    " mean_ms="+std::to_string(sum/materialTimes.size())+" fps="+std::to_string(1000.0*materialTimes.size()/sum)+
                    " p95_ms="+std::to_string(materialTimes[(materialTimes.size()-1)*95/100]));
            }
        }
        if(snowCycle && probeFrame>=600) {
            int local=(probeFrame-600)%600;
            if(local==0)snowTimes.clear();
            if(local>=120)snowTimes.push_back(std::chrono::duration<double,std::milli>(profileDrawEnd-profileGameStart).count());
            if(local==599) {
                double sum=0;for(double ms:snowTimes)sum+=ms;
                std::sort(snowTimes.begin(),snowTimes.end());
                LogMessage("RUNE_SNOW_PERFORMANCE block="+std::to_string((probeFrame-600)/600)+" mode="+
                    std::to_string(RuneSnow::Mode.load())+" frames="+std::to_string(snowTimes.size())+
                    " mean_ms="+std::to_string(sum/snowTimes.size())+" fps="+std::to_string(1000.0*snowTimes.size()/sum)+
                    " p95_ms="+std::to_string(snowTimes[(snowTimes.size()-1)*95/100]));
            }
        }
		++probeFrame;
        if(probeFrame==1 && commandline->HasArg("","--probe-position")) {
            if(!LaunchInfo.IsRune() || !remainingFrames || !commandline->HasArg("","--save-directory"))
                Exception::Throw("Position probe requires bounded Rune run and isolated saves");
            auto value=commandline->GetArg("","--probe-position","");
            std::replace(value.begin(),value.end(),',',' ');std::istringstream input(value);
            vec3 p;int yaw=0,pitch=0;std::string extra;
            if(!(input>>p.x>>p.y>>p.z>>yaw>>pitch) || (input>>extra) ||
                !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
                yaw < -65536 || yaw > 65536 || pitch < -16384 || pitch > 16384 ||
                !viewport->Actor()->SetLocation(p))Exception::Throw("Invalid or blocked position probe");
            auto* pawn=viewport->Actor();pawn->Velocity()=vec3(0);
            pawn->SetRotation(Rotator(0,yaw,0));pawn->ViewRotation()=Rotator(pitch,yaw,0);
        }
		if (probeFrame == 1 && commandline->HasArg("", "--probe-start") && LaunchInfo.IsRune() && viewport->Actor())
		{
			// Select a placed start for opt-in encounter fixtures; do not bypass
			// world collision or alter ordinary campaign spawning.
			NameString requested = commandline->GetArg("", "--probe-start", "");
			UActor* start = nullptr;
			for (auto* actor : Level->Actors)
				if (actor && actor->Name == requested && actor->IsA("PlayerStart")) { start = actor; break; }
			auto* player = viewport->Actor();
			if (!start || !player->SetLocation(start->Location())) Exception::Throw("Encounter start is unavailable");
			player->SetRotation(start->Rotation());
			player->ViewRotation() = start->Rotation();
			player->Velocity() = vec3(0);
			LogMessage("RUNE_ENCOUNTER_START name="+requested.ToString());
		}
        if(probeFrame==60 && commandline->HasArg("","--probe-gong")) {
            if(!LaunchInfo.IsRune() || !commandline->HasArg("","--save-directory") || !commandline->HasArg("","--maxframes"))
                Exception::Throw("Gong fixture requires bounded Rune run and isolated saves");
            UActor* gong=nullptr;
            for(auto* a:Level->Actors)if(a && !a->bDeleteMe() && a->IsA("Gong") && !a->Event().IsNone()) {
                if(gong)Exception::Throw("Gong fixture needs one linked gong");gong=a;
            }
            if(!gong)Exception::Throw("No linked gong in fixture map");
            CallEvent(gong,"PlayInstrument",{ExpressionValue::ObjectValue(viewport->Actor())});
            LogMessage("RUNE_GONG_FIXTURE actor="+gong->Name.ToString());
        }
        if (probeFrame == 120 && commandline->HasArg("", "--probe-texture-pack"))
        {
            if(!commandline->HasArg("", "--save-directory")) Exception::Throw("Texture probe requires isolated saves");
            OutputDevice output;
            if(!window->GetRenderDevice()->Exec("RuneTexturePackProbe",output)) Exception::Throw("Texture probe requires Vulkan");
        }
        if(probeFrame==120 && commandline->HasArg("","--probe-modern-water-only")) {
            if(!commandline->HasArg("","--save-directory") || !remainingFrames)Exception::Throw("Modern water probe requires bounded isolated saves");
            OutputDevice output;
            if(!window->GetRenderDevice()->Exec("RuneModernWaterProbe",output))Exception::Throw("Modern water probe requires Vulkan");
        }
        if(probeFrame==120 && commandline->HasArg("","--probe-material-lighting")) {
            if(!commandline->HasArg("","--save-directory"))Exception::Throw("Material probe requires isolated saves");
            OutputDevice output;
            if(!window->GetRenderDevice()->Exec("RuneMaterialLightingProbe",output))Exception::Throw("Material probe requires Vulkan");
        }
		if (probeFrame == 120 && (commandline->HasArg("", "--probe-vulkan")||commandline->HasArg("","--probe-shadow-budget")))
		{
			if(!commandline->HasArg("","--save-directory"))Exception::Throw("GPU probe requires isolated saves");
			OutputDevice output;
			if (!window->GetRenderDevice()->Exec(commandline->HasArg("","--probe-shadow-budget")?"RuneShadowBudgetProbe":"RuneVulkanProbe", output))
				Exception::Throw("Vulkan probe requires Vulkan renderer");
		}
		if (commandline->HasArg("", "--probe-camera") && LaunchInfo.IsRune() && viewport->Actor() && probeFrame%60==0)
		{
			auto* pawn=viewport->Actor();
			if(commandline->HasArg("", "--trace-rune-combat")) {
				auto* proxy=UObject::TryCast<UActor>(pawn->GetUObject("AnimProxy"));
				auto* held=pawn->GetUObject("Weapon");
				LogMessage("RUNE_PLAYER_ATTACK proxy="+(proxy?proxy->GetStateName().ToString():"None")+
					" anim="+(proxy?proxy->AnimSequence().ToString():"None")+" frame="+std::to_string(proxy?proxy->AnimFrame():0)+
					" weapon="+(held?held->Name.ToString():"None")+" weapon_state="+(held?held->GetStateName().ToString():"None")+
					" fire="+std::to_string(pawn->bFire()));
				for(auto* actor:Level->Actors) if(auto* other=UObject::TryCast<UPawn>(actor);other && other!=pawn && !other->bDeleteMe() && length(other->Location()-pawn->Location())<600)
					LogMessage("RUNE_NEARBY_PAWN name="+other->Name.ToString()+" state="+other->GetStateName().ToString()+" health="+std::to_string(other->Health())+
						" x="+std::to_string(other->Location().x)+" y="+std::to_string(other->Location().y)+" z="+std::to_string(other->Location().z));
			}
			UActor* camera=pawn->ViewTarget();
			UActor* point=camera?camera->Target():nullptr;
			LogMessage("RUNE_CAMERA frame="+std::to_string(probeFrame)+" map="+LevelInfo->URL.Map+" state="+pawn->GetStateName().ToString()+" physics="+std::to_string(pawn->Physics())+" distance="+std::to_string(pawn->GetFloat("CurrentDist"))+
				" alpha="+std::to_string(pawn->GetFloat("ClientSideAlphaScale"))+" fading="+std::to_string(pawn->GetBool("bClientSideAlpha"))+
				" actor_yaw="+std::to_string(pawn->Rotation().Yaw)+" view_yaw="+std::to_string(pawn->ViewRotation().Yaw)+
				" x="+std::to_string(pawn->Location().x)+" y="+std::to_string(pawn->Location().y)+" z="+std::to_string(pawn->Location().z)+
				" health="+std::to_string(pawn->Health())+" view_pitch="+std::to_string(pawn->ViewRotation().Pitch)+
				" velocity="+std::to_string(pawn->Velocity().x)+","+std::to_string(pawn->Velocity().y)+","+std::to_string(pawn->Velocity().z)+
				" water="+std::to_string(bool(pawn->Region().Zone->bWaterZone()))+
				" head_water="+std::to_string(bool(pawn->HeadRegion().Zone->bWaterZone()))+
				" surface_swim="+std::to_string(pawn->GetBool("bSurfaceSwimming"))+
				" grab_edges="+std::to_string(pawn->GetBool("bCanGrabEdges"))+
				" camera="+(camera?camera->Name.ToString():"None")+" point="+(point?point->Name.ToString():"None")+
				" view="+std::to_string(CameraLocation.x)+","+std::to_string(CameraLocation.y)+","+std::to_string(CameraLocation.z)+","+std::to_string(CameraRotation.Yaw)+","+std::to_string(CameraRotation.Pitch)+" path_alpha="+std::to_string(camera?camera->PhysAlpha():0.0f)+" path_active="+std::to_string(camera?bool(camera->bInterpolating()):false));
		}
        if(profileFrames>0 && probeFrame>60 && (int)profileTimes.size()<profileFrames) {
            auto ms=[](auto begin,auto end){return std::chrono::duration<double,std::milli>(end-begin).count();};
            double pacing=ms(profileStart,profileGameStart),tick=ms(profileGameStart,profileDrawStart),draw=ms(profileDrawStart,profileDrawEnd);
            profilePacing+=pacing; profileTick+=tick; profileRender+=draw; profileTimes.push_back(pacing+tick+draw);
            if((int)profileTimes.size()==profileFrames) {
                std::sort(profileTimes.begin(),profileTimes.end());
                double mean=(profilePacing+profileTick+profileRender)/profileFrames;
                LogMessage("RUNE_PROFILE frames="+std::to_string(profileFrames)+" fps="+std::to_string(1000.0/mean)+" mean_ms="+std::to_string(mean)+" p50_ms="+std::to_string(profileTimes[profileFrames/2])+" p95_ms="+std::to_string(profileTimes[(profileFrames-1)*95/100])+" pacing_ms="+std::to_string(profilePacing/profileFrames)+" tick_ms="+std::to_string(profileTick/profileFrames)+" render_ms="+std::to_string(profileRender/profileFrames));
                const auto& particles = RuneParticleProfile::Milliseconds;
                LogMessage("RUNE_PARTICLE_PROFILE tick_ms="+std::to_string(particles[RuneParticleProfile::Tick]/profileFrames)+" bounds_ms="+std::to_string(particles[RuneParticleProfile::Bounds]/profileFrames)+" draw_ms="+std::to_string(particles[RuneParticleProfile::Draw]/profileFrames));
            }
        }
		if (probeFrame==30 && !targetClass.empty() && LaunchInfo.IsRune() && viewport->Actor())
		{
			auto* player=viewport->Actor();
			vec3 forward=(Coords::Rotation(player->Rotation()).ToMatrix()*vec4(1,0,0,0)).xyz();
			combatProbeTarget=player->Spawn(packages->GetPackage("RuneI")->GetClass(targetClass),{}, {},player->Location()+forward*65.0f+vec3(0,0,10),Rotator(0,0,0));
			if(!combatProbeTarget) Exception::Throw("Could not spawn combat probe target");
			combatProbeTarget->SetPhysics(PHYS_None);
			combatProbeTarget->SetCollision(true,false,false);
			combatProbeTarget->SetBool("bSweepable",true);
			combatProbeTarget->SetBool("bDestroyable",true);
			LogMessage("RUNE_COMBAT_TARGET_SPAWN class="+targetClass);
		}
		if (probeFrame-combatStartFrame==120 && !shieldClass.empty() && LaunchInfo.IsRune() && viewport->Actor())
		{
			auto* player=viewport->Actor();
			auto* shield=player->Spawn(packages->GetPackage("RuneI")->GetClass(shieldClass),{}, {},player->Location(),player->Rotation());
			if (!shield) Exception::Throw("Could not spawn combat probe shield");
			// Equip through the game's pickup routines; DM can disable auto-touch
			// pickup, so this fixture grants equipment without changing that setting.
			auto* copy=CallEvent(shield,"SpawnCopy",{ExpressionValue::ObjectValue(player)}).ToObject();
			if (!copy) Exception::Throw("Could not grant combat probe shield");
			CallEvent(player,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});
			copy->GotoState("Idle", "Begin");
			LogMessage("RUNE_SHIELD_PICKUP state="+shield->GetStateName().ToString()+" equipped="+std::to_string(player->GetUObject("Shield")!=nullptr));
		}
		if (probeFrame-combatStartFrame==enemySpawnFrame && !enemyClass.empty() && LaunchInfo.IsRune() && viewport->Actor())
		{
			auto* player=viewport->Actor();
			vec3 forward=(Coords::Rotation(player->Rotation()).ToMatrix()*vec4(1,0,0,0)).xyz();
			if (commandline->HasArg("", "--probe-enemy-clear-lane"))
			{
				if (!commandline->HasArg("", "--save-directory")) Exception::Throw("Clear-lane fixture requires isolated saves");
				bool found = false;
				for (int angle = 0; angle < 16 && !found; angle++)
				{
					float rad = angle * 3.14159265f / 8;
					forward = vec3(std::cos(rad), std::sin(rad), 0);
					auto start = player->Location(), nearValue = start + forward * 75.0f, end = start + forward * 200.0f;
					found = player->CheckLocation(nearValue,22,42,true).first && player->CheckLocation(end,22,42,true).first && player->FastTrace(end,start);
				}
				if (!found) Exception::Throw("No clear enemy fixture lane");
				auto rotation = Rotator::FromVector(forward);
				player->SetRotation(rotation); player->ViewRotation() = rotation;
				LogMessage("RUNE_ENEMY_CLEAR_LANE yaw=" + std::to_string(rotation.Yaw));
			}
			combatEnemy=UObject::TryCast<UPawn>(player->Spawn(packages->GetPackage("RuneI")->GetClass(enemyClass),{}, {},
				player->Location()+forward*140.0f+vec3(0,0,10),Rotator(0,player->Rotation().Yaw+32768,0)));
			if(!combatEnemy) Exception::Throw("Could not spawn combat probe enemy");
			LogMessage("RUNE_ENEMY_SPAWN class="+enemyClass+" health="+std::to_string(combatEnemy->Health()));
			LogMessage("RUNE_ENEMY_GEOMETRY radius="+std::to_string(combatEnemy->CollisionRadius())+" height="+std::to_string(combatEnemy->CollisionHeight())+
				" prepivot_z="+std::to_string(combatEnemy->PrePivot().z)+" frame_notifies="+std::to_string(combatEnemy->GetBool("bFrameNotifies")));
			if (commandline->HasArg("", "--probe-check-reach"))
			{
				vec3 before=combatEnemy->Location();
				if (!combatEnemy->ReachableWalking(player) || combatEnemy->Location()!=before)
					Exception::Throw("Walking reachability must accept pawn contact without moving the pawn");
				auto* actorClass=packages->GetPackage("Engine")->GetClass("Actor");
				auto* elevated=UObject::Cast<UActor>(packages->GetTransientPackage()->NewObject("RuneElevatedGoal",actorClass,ObjectFlags::Transient));
				elevated->Location()=player->Location()+vec3(0,0,512);
				if (combatEnemy->ReachableWalking(elevated) || combatEnemy->Location()!=before)
					Exception::Throw("Walking reachability accepted an elevated unreachable goal");
				auto* blocker=player->Spawn(packages->GetPackage("RuneI")->GetClass("Stool"),{}, {},(player->Location()+before)*0.5f,Rotator(0,0,0));
				if (!blocker) Exception::Throw("Could not spawn reachability obstacle");
				blocker->SetCollisionSize(40,256);
				blocker->SetCollision(true,true,true);
				bool blocked=combatEnemy->ReachableWalking(player);
				blocker->Destroy();
				if (blocked || combatEnemy->Location()!=before) Exception::Throw("Walking reachability ignored an intervening obstacle");
				LogMessage("RUNE_REACHABILITY_OK pawn_contact=1 elevated=0 obstructed=0 location_preserved=1");
			}
		}
		if (combatEnemy && probeFrame%60==0 && viewport->Actor())
		{
			auto* player=viewport->Actor();
			auto* weapon=combatEnemy->GetUObject("Weapon");
			auto* shield=player->GetUObject("Shield");
			auto* proxy=player->GetUObject("AnimProxy");
			LogMessage("RUNE_ENCOUNTER frame="+std::to_string(probeFrame)+" enemy_health="+std::to_string(combatEnemy->Health())+
				" enemy_state="+combatEnemy->GetStateName().ToString()+" enemy_anim="+combatEnemy->AnimSequence().ToString()+
				" enemy_weapon="+(weapon?weapon->Class->Name.ToString():"None")+" enemy_destroyed="+std::to_string((bool)combatEnemy->bDeleteMe())+
				" player_health="+std::to_string(player->Health())+" player_state="+player->GetStateName().ToString()+
				" player_proxy="+(proxy?proxy->GetStateName().ToString():"None")+" player_shield="+(shield?shield->Class->Name.ToString():"None")+
				" shield_health="+std::to_string(shield?shield->GetInt("Health"):0)+
				" weapon_state="+(weapon?weapon->GetStateName().ToString():"None")+
				" player_xyz="+std::to_string(player->Location().x)+","+std::to_string(player->Location().y)+","+std::to_string(player->Location().z)+
				" enemy_xyz="+std::to_string(combatEnemy->Location().x)+","+std::to_string(combatEnemy->Location().y)+","+std::to_string(combatEnemy->Location().z)+
				" distance="+std::to_string(length(combatEnemy->Location()-player->Location()))+
                " tactical="+std::to_string(combatEnemy->Tactics.active)+" decisions="+std::to_string(combatEnemy->Tactics.decisions)+
                " attacks="+std::to_string(combatEnemy->Tactics.attacks)+" kicks="+std::to_string(combatEnemy->Tactics.kicks)+" bashes="+std::to_string(combatEnemy->Tactics.bashes));
		}
		advancedCombatProbe.Tick(*this,probeFrame,combatEnemy);
		if (blockFrame>0 && probeFrame==blockFrame) OnWindowKeyDown(EInputKey::IK_RControl);
		if (blockFrame>0 && probeFrame==blockFrame+blockHold) OnWindowKeyUp(EInputKey::IK_RControl);
		if (fireProbeFrame>0 && viewport->Actor())
		{
			int attackFrame=probeFrame-fireProbeFrame;
			bool repeatAttack=fireRepeat>fireHoldFrames && viewport->Actor()->Health()>0;
			auto logProxy=[&]()
			{
				auto* proxy=UObject::TryCast<UActor>(viewport->Actor()->GetUObject("AnimProxy"));
				LogMessage("RUNE_FIRE_PROXY state="+(proxy?proxy->GetStateName().ToString():"None")+" animation="+(proxy?proxy->AnimSequence().ToString():"None")+" rate="+std::to_string(proxy?proxy->AnimRate():0)+" frame="+std::to_string(proxy?proxy->AnimFrame():0));
			};
			if (probeFrame==fireProbeFrame || (repeatAttack && attackFrame>0 && attackFrame%fireRepeat==0))
			{
				auto* weapon=viewport->Actor()->GetUObject("Weapon");
				LogMessage("RUNE_FIRE_BEGIN weapon="+(weapon?weapon->Class->Name.ToString():"None")+" state="+viewport->Actor()->GetStateName().ToString());
				if(combatProbeTarget) LogMessage("RUNE_COMBAT_TARGET_BEFORE_FIRE destroyed="+std::to_string((bool)combatProbeTarget->bDeleteMe()));
				OnWindowKeyDown(EInputKey::IK_LeftMouse);
				logProxy();
			}
			if (probeFrame==fireProbeFrame+20) logProxy();
			if (probeFrame==fireProbeFrame+fireHoldFrames || (fireRepeat>fireHoldFrames && attackFrame>0 && attackFrame%fireRepeat==fireHoldFrames))
			{
				OnWindowKeyUp(EInputKey::IK_LeftMouse);
				auto* weapon=viewport->Actor()->GetUObject("Weapon");
				LogMessage("RUNE_FIRE_END weapon="+(weapon?weapon->Class->Name.ToString():"None")+" state="+(weapon?weapon->GetStateName().ToString():"None"));
				if(combatProbeTarget) LogMessage("RUNE_COMBAT_TARGET_RESULT destroyed="+std::to_string((bool)combatProbeTarget->bDeleteMe()));
			}
		}
		if (firstCall)
		{
			if (commandline->HasArg("", "--close-menu")) CallEvent(console,"CloseUWindow");
			if (forwardProbeFrames>0 && viewport->Actor())
			{
				forwardProbeStart=viewport->Actor()->Location();
				OnWindowKeyDown(EInputKey::IK_W);
				LogMessage("RUNE_INPUT_BEGIN binding="+keybindings["W"]+" state="+viewport->Actor()->GetStateName().ToString()+" activeAxes="+std::to_string(activeInputAxes.size()));
			}
			firstCall=false;
		}
		else if (forwardProbeFrames>0 && --forwardProbeFrames==0 && viewport->Actor())
		{
			OnWindowKeyUp(EInputKey::IK_W);
			vec3 end=viewport->Actor()->Location();
			LogMessage("RUNE_INPUT_END state="+viewport->Actor()->GetStateName().ToString()+" paused="+std::to_string(m_GamePaused));
			LogMessage("RUNE_INPUT_MOVEMENT start="+std::to_string(forwardProbeStart.x)+","+std::to_string(forwardProbeStart.y)+","+std::to_string(forwardProbeStart.z)+" end="+std::to_string(end.x)+","+std::to_string(end.y)+","+std::to_string(end.z)+" distance="+std::to_string(length(end-forwardProbeStart)));
		}
		if (remainingFrames > 0 && --remainingFrames == 0)
		{
            goreProbe.Finish();
            corpseProbe.Finish();
            physicsProbe.Finish();
            slamProbe.Finish();
			if(saveProbe.Active()) Exception::Throw("Bounded run ended before save probe completed");
			if(ropeProbe.Active()) Exception::Throw("Bounded run ended before rope probe completed");
			if(gibProbe.Active()) Exception::Throw("Bounded run ended before gib probe completed");
            if(kickProbe.Active()) Exception::Throw("Bounded run ended before kick probe completed");
            if(bashProbe.Active()) Exception::Throw("Bounded run ended before bash probe completed");
			std::string capture = commandline->GetArg("", "--capture-frame", "");
			if (!capture.empty())
			{
                auto captureFrame = [&](const std::string& filename) {
                    auto* device = window->GetRenderDevice();
                    int w = device->GetRenderWidth(), h = device->GetRenderHeight();
                    Array<TextureColor> pixels; pixels.resize((size_t)w*h);
                    bool oldGammaScreenshots = device->GammaCorrectScreenshots;
                    if(commandline->HasArg("", "--capture-present-frame")) device->GammaCorrectScreenshots = true;
                    device->ReadPixels(pixels.data());
                    device->GammaCorrectScreenshots = oldGammaScreenshots;
                    std::ofstream image(filename,std::ios::binary);
                    if (!image) Exception::Throw("Cannot create frame capture");
                    image << "P6\n" << w << ' ' << h << "\n255\n";
                    for (const auto& p:pixels) { image.put((char)p.R); image.put((char)p.G); image.put((char)p.B); }
                };
                captureFrame(capture);
                if(commandline->HasArg("","--capture-gore-material-comparison")) {
                    if(!commandline->HasArg("","--save-directory"))Exception::Throw("Material-cache comparison requires isolated saves");
                    int saved=RuneGore::MaterialBatchOverride;
                    for(int block=0;block<4;++block) {
                        int mode=(block==1||block==2)?1:0;RuneGore::MaterialBatchOverride=mode;
                        double sum=0;int samples=0;
                        // Freeze simulation, camera, lighting and animation. Only
                        // the actor-classification scheduling changes between draws.
                        for(int i=0;i<90;++i){auto begin=std::chrono::steady_clock::now();render->DrawGame(0);if(i>=10){sum+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();++samples;}}
                        LogMessage("RUNE_GORE_MATERIAL_BENCH block="+std::to_string(block)+" hoisted="+std::to_string(mode)+" mean_ms="+std::to_string(sum/samples));
                        if(block<2)captureFrame(capture+(mode?"-hoisted.ppm":"-original.ppm"));
                    }
                    RuneGore::MaterialBatchOverride=saved;
                }
                if(commandline->HasArg("","--capture-gore-comparison") || commandline->HasArg("","--probe-gore-render")) {
                    if(!commandline->HasArg("","--save-directory"))Exception::Throw("Gore comparison requires isolated saves");
                    bool saved=RuneGore::Enabled.load();
                    for(int block=0;block<4;++block) {
                        int mode=(block==1 || block==2)?1:0;RuneGore::Enabled=mode!=0;
                        int frames=commandline->HasArg("","--probe-gore-render")?135:1;
                        double total=0;int samples=0;
                        for(int i=0;i<frames;++i) {
                            auto begin=std::chrono::steady_clock::now();render->DrawGame(0);
                            if(i>=15){total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();++samples;}
                        }
                        if(samples)LogMessage("RUNE_GORE_RENDER_BENCH block="+std::to_string(block)+" enabled="+std::to_string(mode)+" frames="+std::to_string(samples)+" mean_ms="+std::to_string(total/samples));
                        if(block<2)captureFrame(capture+(mode?"-gore-on.ppm":"-gore-off.ppm"));
                    }
                    RuneGore::Enabled=saved;
                }
                if(commandline->HasArg("","--capture-material-comparison")) {
                    if(!commandline->HasArg("","--save-directory"))Exception::Throw("Material capture requires isolated saves");
                    auto* device=window->GetRenderDevice();int saved=device->WorldFeatures;
                    for(int mode:{0,196608}) {
                        device->WorldFeatures=(saved&~196608)|mode;
                        render->DrawGame(0);
                        captureFrame(capture+(mode?"-on.ppm":"-off.ppm"));
                    }
                    device->WorldFeatures=saved;
                }
                if(commandline->HasArg("", "--capture-texture-comparison"))
                {
                    if(!commandline->HasArg("", "--save-directory")) Exception::Throw("Texture comparison requires isolated saves");
                    auto* device=window->GetRenderDevice();int saved=device->TexturePackQuality;
                    for(int quality : {0,saved})
                    {
                        device->Flush(false);device->TexturePackQuality=quality;
                        render->DrawGame(0.0f);
                        captureFrame(capture+(quality?"-textures-on.ppm":"-textures-off.ppm"));
                    }
                    LogMessage("RUNE_TEXTURE_CAPTURE_OK same_camera=1 same_simulation_time=1");
                }
                if (commandline->HasArg("", "--capture-lighting-comparison"))
                {
                    if (!commandline->HasArg("", "--save-directory")) Exception::Throw("Lighting capture requires isolated saves");
                    auto* device = window->GetRenderDevice();
                    int saved = device->WorldFeatures;
                    for (int mode = 0; mode < 2; mode++)
                    {
                        device->WorldFeatures = (saved & ~768) | (mode ? ((saved & 512) ? 768 : 256) : 0);
                        // Same camera, pose and simulation time for both images.
                        render->DrawGame(0.0f);
                        captureFrame(capture + (mode ? "-on.ppm" : "-off.ppm"));
                    }
                    device->WorldFeatures = saved;
                    LogMessage("RUNE_LIGHTING_CAPTURE_OK same_camera=1 same_simulation_time=1");
                }
			}
			LogMessage("BOUNDED_RUN_COMPLETE");
			if(combatProbeTarget) LogMessage("RUNE_COMBAT_TARGET_FINAL destroyed="+std::to_string((bool)combatProbeTarget->bDeleteMe()));
			quit=true;
		}

		// Save the game if there is a request for it
		if (sessionSavePending)
		{
			// A checkpoint queued during Level.Tick must not replace a user save.
			SaveGameToSlot(sessionSaveSlot,sessionSaveSlot==9 ? "Quick save" : "Manual save "+std::to_string(sessionSaveSlot+1));
			if (SaveGameInfo.SaveGameSlot==9) {
				SaveGameInfo.SaveGameSlot=DONT_SAVE_GAME;
				SaveGameInfo.SaveGameDescription.clear();
			}
			RuneCombat::SessionResult.store(1);
			sessionSavePending=false;
			LogMessage("RUNE_SESSION_SAVE_OK slot="+std::to_string(sessionSaveSlot)+" map="+LevelInfo->URL.Map);
		}
		if (SaveGameInfo.SaveGameSlot != DONT_SAVE_GAME)
		{
			if(!titleIdle) SaveGameToSlot(SaveGameInfo.SaveGameSlot, SaveGameInfo.SaveGameDescription);

			SaveGameInfo.SaveGameSlot = DONT_SAVE_GAME;
			SaveGameInfo.SaveGameDescription.clear();
		}

		// Check if there is a new map to load
		if (!menuOpen && !LevelInfo->NextURL().empty())
		{
			LevelInfo->NextSwitchCountdown() -= levelElapsed;
			if (LevelInfo->NextSwitchCountdown() <= 0.0f)
			{
				// LoginPlayer only transfers travel actors when ClientTravelInfo.TravelType is
				// TRAVEL_Relative, and TravelType is otherwise assigned only in ClientTravel. These
				// NextURL routes never go through ClientTravel, so without setting it here they
				// inherit whatever the last ClientTravel left behind (or TRAVEL_Absolute, the
				// default) - which silently discarded the inventory the bNextItems branch exists
				// specifically to carry. State the intent explicitly on each branch instead of
				// depending on leftover state.
				if (UnrealURL(LevelInfo->NextURL()).HasOption("restart"))
				{
					// Passes the level's own TravelInfo back in, so it means to preserve it.
					ClientTravelInfo.TravelType = ETravelType::TRAVEL_Relative;
					LoadMap(LevelInfo->URL, Level->TravelInfo);
					LoginPlayer();
				}
				else if (LevelInfo->bNextItems())
				{
					ClientTravelInfo.TravelType = ETravelType::TRAVEL_Relative;
					LoadMap(UnrealURL(LevelInfo->URL, LevelInfo->NextURL()), CreateTravelInfo(true));
					LoginPlayer();
				}
				else
				{
					// Deliberately carries nothing - it passes no travel info at all.
					ClientTravelInfo.TravelType = ETravelType::TRAVEL_Absolute;
					LoadMap(UnrealURL(LevelInfo->URL, LevelInfo->NextURL()), {});
					LoginPlayer();
				}
			}
		}

		if (ClientTravelInfo.URL.HasOption("restart"))
		{
			LoadMap(LevelInfo->URL, Level->TravelInfo);
			LoginPlayer();
		}

		if (ClientTravelInfo.URL.HasOption("load"))
		{
			UnrealURL url(ClientTravelInfo.URL);
			LoadFromSaveFile(url);
			PossessSavedPlayer();
		}

		if (!ClientTravelInfo.URL.Map.empty())
		{
			// To do: need to do something about that travel type and transfering of items

			UnrealURL url(ClientTravelInfo.URL);
			LogMessage("Client travel to " + url.ToString());
			LoadMap(url, CreateTravelInfo(ClientTravelInfo.TransferItems));
			LoginPlayer();
		}

		if (ClientTravelInfo.TravelToEntryLevel)
		{
			ClientTravelInfo.TravelToEntryLevel = false;
			if (EntryLevel && Level != EntryLevel)
			{
				LogMessage("Client travel to entry");
				UnloadMap();
				LevelPackage = EntryLevelPackage;
				Level = EntryLevel;
				LevelInfo = EntryLevelInfo;
				GameInfo = EntryGameInfo;
				LoginPlayer();
			}
		}
		saveProbe.Tick(*this,probeFrame);
		ropeProbe.Tick(*this,probeFrame);
		gibProbe.Tick(*this,probeFrame);
        kickProbe.Tick(*this,probeFrame);
        feedbackProbe.Tick(*this,probeFrame);
        bashProbe.Tick(*this,probeFrame);
	}

	LogMessage("Shutting down...");
	window->UnlockCursor();

	LogMessage("Saving configurations...");
	if (packages->MissingSESystemIni())
	{
		// Add the missing Subsystem entries
		client->SaveConfig();
		audiodev->SaveConfig();
		renderdev->SaveConfig();
	}
	packages->SetIniValue("System", "Engine.SurrealWindowSystem", "WindowSystem", windowingSystemName);
	// Isolated validation and hosted sessions keep normal preferences intact.
	if (!commandline->HasArg("", "--save-directory")) packages->SaveAllIniFiles();

	LogMessage("Closing window...");
	CloseWindow();
}

void Engine::PlayAVI(const Array<std::string>& args)
{
	if (args.size() < 3)
		return;

	// What did KHG request?
	std::string buildup, breakdown, video;
	if (args.size() > 4 && args[2] == "C")
	{
		buildup = args[1];
		video = args[3];
		breakdown = "breakdn.avi";
	}
	else if (args.size() > 3 && args[2] == "Y")
	{
		buildup = "buildup.avi";
		video = args[1];
		breakdown = "breakdn.avi";
	}
	else
	{
		video = args[1];
	}

	playingAvi = true;
	skipAvi = false;

	try
	{
		// Load the videos

		std::unique_ptr<VideoPlayer> buildupPlayer, videoPlayer, breakdownPlayer;
		if (!buildup.empty())
			buildupPlayer = VideoPlayer::Create(packages->GetVideoFilename(buildup));
		if (!video.empty())
			videoPlayer = VideoPlayer::Create(packages->GetVideoFilename(video));
		if (!breakdown.empty())
			breakdownPlayer = VideoPlayer::Create(packages->GetVideoFilename(breakdown));

		CalcTimeElapsed(); // Reset so load time doesn't affect playback

		UnrealMipmap* background = nullptr;
		if (buildupPlayer)
		{
			background = PlayVideo(buildupPlayer.get(), nullptr);
			if (!background)
			{
				CalcTimeElapsed();
				return;
			}

			// Cut a hole in the background image
			uint32_t* pixels = (uint32_t*)background->Data.data();
			int w = background->Width;
			int h = background->Height;

			ivec2 v0 = { 232, 125 };
			ivec2 v1 = { 161, 246 };
			ivec2 v2 = { 406, 125 };
			ivec2 v3 = { 475, 246 };
			ivec2 v4 = { 258, 125 };
			ivec2 v5 = { 270, 144 };
			ivec2 v6 = { 380, 125 };
			ivec2 v7 = { 368, 144 };
			ivec2 v8 = { 210, 327 };
			ivec2 v9 = { 427, 327 };

			for (int y = 126; y < 144; y++)
			{
				int x0 = (int)(v0.x + 0.5f + (y - v0.y + 0.5f) * (v1.x - v0.x) / (v1.y - v0.y));
				int x1 = (int)(v4.x + 0.5f + (y - v4.y + 0.5f) * (v5.x - v4.x) / (v5.y - v4.y));
				int x2 = (int)(v6.x + 0.5f + (y - v6.y + 0.5f) * (v7.x - v6.x) / (v7.y - v6.y));
				int x3 = (int)(v2.x + 0.5f + (y - v2.y + 0.5f) * (v3.x - v2.x) / (v3.y - v2.y));

				pixels[x0 + y * w] = 0x80000000;
				pixels[x1 - 1 + y * w] = 0x80000000;
				for (int x = x0 + 1; x < x1 - 1; x++)
					pixels[x + y * w] = 0;

				pixels[x2 + y * w] = 0x80000000;
				pixels[x3 - 1 + y * w] = 0x80000000;
				for (int x = x2 + 1; x < x3 - 1; x++)
					pixels[x + y * w] = 0;
			}

			for (int y = 144; y < 246; y++)
			{
				int x0 = (int)(v0.x + 0.5f + (y - v0.y + 0.5f) * (v1.x - v0.x) / (v1.y - v0.y));
				int x1 = (int)(v2.x + 0.5f + (y - v2.y + 0.5f) * (v3.x - v2.x) / (v3.y - v2.y));
				pixels[x0 + y * w] = 0x80000000;
				pixels[x1 - 1 + y * w] = 0x80000000;
				for (int x = x0 + 1; x < x1 - 1; x++)
					pixels[x + y * w] = 0;
			}

			for (int y = 246; y < 325; y++)
			{
				int x0 = (int)(v1.x + 0.5f + (y - v1.y + 0.5f) * (v8.x - v1.x) / (v8.y - v1.y));
				int x1 = (int)(v3.x + 0.5f + (y - v3.y + 0.5f) * (v9.x - v3.x) / (v9.y - v3.y));
				pixels[x0 + y * w] = 0x80000000;
				pixels[x1 - 1 + y * w] = 0x80000000;
				for (int x = x0 + 1; x < x1 - 1; x++)
					pixels[x + y * w] = 0;
			}
		}

		if (videoPlayer)
		{
			if (!PlayVideo(videoPlayer.get(), background))
			{
				playingAvi = false;
				CalcTimeElapsed();
				return;
			}
		}

		if (breakdownPlayer)
		{
			PlayVideo(breakdownPlayer.get(), nullptr);
		}
	}
	catch (const std::exception& e)
	{
		LogMessage("Error playing " + video + ": " + e.what());
	}

	playingAvi = false;
	CalcTimeElapsed(); // Reset so game isn't affected
}

UnrealMipmap* Engine::PlayVideo(VideoPlayer* video, UnrealMipmap* background)
{
	UnrealMipmap* frame = nullptr;

	TextureInfo texinfo[2];
	texinfo[0].CacheID = 0xffffffff'ffffffffULL;
	texinfo[0].Format = TextureFormat::BGRA8;
	texinfo[0].NumMips = 1;

	if (background)
	{
		texinfo[1].CacheID = 0xffffffff'fffffffeULL;
		texinfo[1].Format = TextureFormat::BGRA8;
		texinfo[1].NumMips = 1;
		texinfo[1].Mips = background;
		texinfo[1].USize = background->Width;
		texinfo[1].VSize = background->Height;
		texinfo[1].bRealtimeChanged = true;
	}

	audiodev->SetViewport(nullptr);
	audiodev->GetDevice()->PlayMusic(video->GetAudio());

	float timestamp = 0.0f;
	int curframe = -1;
	while (!quit && !skipAvi)
	{
		timestamp += CalcTimeElapsed();

		bool done = false;
		while (curframe < video->GetFrameIndexForTime(timestamp))
		{
			while (true)
			{
				UnrealMipmap* nextframe = video->NextVideoFrame();
				if (nextframe)
				{
					frame = nextframe;
					curframe++;
					texinfo[0].bRealtimeChanged = true;
					break;
				}
				if (!video->Decode())
				{
					done = true;
					break;
				}
			}
			if (done)
				break;
		}
		if (done)
			break;

		audiodev->GetDevice()->Update();
		GameWindow::ProcessEvents();

		if (frame)
		{
			texinfo[0].Mips = frame;
			texinfo[0].USize = frame->Width;
			texinfo[0].VSize = frame->Height;

			viewport->SetViewportRect(0, 0, engine->window->GetPixelWidth(), engine->window->GetPixelHeight());
			render->DrawVideoFrame(&texinfo[0], background ? &texinfo[1] : nullptr);
		}
	}

	audiodev->GetDevice()->PlayMusic(nullptr);

	if (quit || skipAvi)
		return nullptr;

	return frame;
}

UConversationList* Engine::GetDeusExMission()
{
	if (!dxConMissionList || !DeusExLevelInfo)
		return nullptr;

	int missionNumber = DeusExLevelInfo->MissionNumber();
	for (UConItem* item = dxConMissionList->missions(); item; item = item->Next())
	{
		auto mission = UObject::Cast<UConversationList>(item->ConObject());
		if (mission->missionNumber() == missionNumber)
		{
			return mission;
		}
	}
	return nullptr;
}

void Engine::UpdateAudio()
{
	mat4 translate = mat4::translate(vec3(0.0f) - CameraLocation);
	mat4 listener = Coords::ViewToAudioDev().ToMatrix() * Coords::Rotation(CameraRotation).ToMatrix() * translate;

	audiodev->SetViewport(viewport);
	audiodev->Update(listener);
}

void Engine::ClientTravel(const std::string& newURL, ETravelType travelType, bool transferItems)
{
	UnrealURL url(newURL);

	if (url.HasOption("entry"))
	{
		// Intro map travels to ?entry when escape is pressed.
		// It wants EntryLevel to be active.
		ClientTravelInfo.TravelToEntryLevel = true;
		return;
	}

	// If the URL doesn't contain the player info, add them here.
	// As they have to persist somehow
	for (std::string optionKey : { "Name", "Class", "team", "skin", "Face", "Voice", "OverrideClass" })
	{
		if (engine->LaunchInfo.ue1Version > 219)
		{
			if (url.HasOption(optionKey))
				engine->packages->SetIniValue("User", "DefaultPlayer", optionKey, url.GetOption(optionKey));
			else
				url.AddOrReplaceOption(optionKey + "=" + packages->GetIniValue("user", "DefaultPlayer", optionKey));
		}
		else
		{
			if (url.HasOption(optionKey))
				engine->packages->SetIniValue("System", "URL", optionKey, url.GetOption(optionKey));
			else
				url.AddOrReplaceOption(optionKey + "=" + packages->GetIniValue("System", "URL", optionKey));
		}
	}

	if (travelType == ETravelType::TRAVEL_Absolute)
		ClientTravelInfo.URL = url;
	else if (travelType == ETravelType::TRAVEL_Partial)
	{
		auto name = ClientTravelInfo.URL.GetOption("name");
		ClientTravelInfo.URL = url;
		ClientTravelInfo.URL.AddOrReplaceOption("name=" + name);
	}
	else if (travelType == ETravelType::TRAVEL_Relative)
	{
		ClientTravelInfo.URL = UnrealURL(ClientTravelInfo.URL, url);
		// Add difficulty if it isn't there
		if (ClientTravelInfo.URL.GetOption("Difficulty").empty())
		{
			ClientTravelInfo.URL.AddOrReplaceOption("Difficulty=" + std::to_string(GameInfo->Difficulty()));
		}
	}

	ClientTravelInfo.TravelType = travelType;
	ClientTravelInfo.TransferItems = transferItems;
}

UnrealURL Engine::GetDefaultURL(const std::string& map)
{
	UnrealURL url;
	std::string teleporterTag = "";
	std::string finalMapName = map;

	size_t tagPos = map.find('#');

	if (tagPos != std::string::npos)
	{
		teleporterTag = map.substr(tagPos + 1);
		finalMapName = map.substr(0, tagPos);
	}
	if (map.find("." + packages->GetMapExtension()) == std::string::npos)
		url.Map = finalMapName + "." + packages->GetMapExtension();
	else
		url.Map = finalMapName;

	if (!teleporterTag.empty())
		url.Portal = teleporterTag;
	for (std::string optionKey : { "Name", "Class", "team", "skin", "Face", "Voice", "OverrideClass" })
	{
		url.Options.push_back(optionKey + "=" + packages->GetIniValue("user", "DefaultPlayer", optionKey));
	}
    if(LaunchInfo.IsRune() && !NetworkAuthority &&
#ifdef OPENRUNE_ENABLE_NETWORK
        !NetworkClient &&
#endif
        (RuneCharacters::Campaign.load()>=0 || commandline->HasArg("","--campaign-character"))) {
        auto choice=RuneCharacters::Selected(true);
        url.AddOrReplaceOption("OpenRuneCharacter="+std::to_string(choice.character));
        url.AddOrReplaceOption("OpenRuneSkin="+std::to_string(choice.skin));
    }
	return url;
}

void Engine::LoadEntryMap()
{
	// The entry map is the map you see in the game when no other map is playing. For example when disconnected from a server. It is always loaded and running.
	const auto entryMapName = packages->GetIniValue("System", "URL", "EntryMap", "Entry");
	LoadMap(GetDefaultURL(entryMapName));
	EntryLevelPackage = LevelPackage;
	EntryLevel = Level;
	EntryLevelInfo = LevelInfo;
	EntryGameInfo = GameInfo;
	LevelPackage = nullptr;
	Level = nullptr;
	LevelInfo = nullptr;
	GameInfo = nullptr;
	viewport->Actor() = nullptr;
}

void Engine::UnloadMap()
{
    RuneNavigation::Reset();
    RunePhysics::Reset();
	if (LevelPackage && LevelPackage != EntryLevelPackage)
		packages->UnloadPackage(LevelPackage);

	LevelPackage = nullptr;
	Level = nullptr;
	LevelInfo = nullptr;
	GameInfo = nullptr;
	DeusExLevelInfo = nullptr;
	dxRootWindow = nullptr;
	viewport->Actor() = nullptr;

	// GC::Collect();
}

void Engine::LoadMap(const UnrealURL& url, const std::map<std::string, std::string>& travelInfo)
{
    RunePhysics::Reset();
    RuneStairPresentation::Reset();
	runePendingButtons.clear();runePulsedButtons.clear();
	ClientTravelInfo.URL.Clear();

	if (Level)
		CallEvent(console, EventName::NotifyLevelChange);

	audiodev->StopSounds();
	UnloadMap();

	// Load map objects

	LevelPackage = packages->LoadMap(url.Map);

	GetLevelInfoObject();

	LevelInfo->ComputerName() = "MyComputer";
	LevelInfo->HubStackLevel() = 0; // To do: handle level hubs
	LevelInfo->EngineVersion() = LaunchInfo.gameVersionString + " SE";
	if (LaunchInfo.ue1Version > 219)
		LevelInfo->MinNetVersion() = LaunchInfo.gameVersionString + " SE";
	LevelInfo->bHighDetailMode() = true;
	LevelInfo->NetMode() = NetworkAuthority ? 1 : 0; // NM_DedicatedServer / NM_StandAlone
	LevelInfo->DefaultTexture() = engine->DefaultTexture;

	LevelInfo->URL = url;

	GetLevelObject();

	Level->TravelInfo = travelInfo; // Initially used travel info for level restart

	// Remove the actors meant for the editor (to do: should we do this at the package manager level?)
	for (UActor*& actor : Level->Actors)
	{
		if (actor && AllFlags(actor->Flags, ObjectFlags::NotForServer))
		{
			actor->bDeleteMe() = true;
			actor = nullptr;
		}
	}

	LinkActorsToLevel();

	// Find the game info class
	UClass* gameInfoClass = packages->FindClass(LevelInfo->URL.GetOption("game"));
	if (!gameInfoClass)
		gameInfoClass = LevelInfo->DefaultGameType();
	if (!gameInfoClass)
		gameInfoClass = packages->FindClass(packages->GetIniValue("system", "Engine.Engine", "DefaultGame"));
	if (!gameInfoClass)
		gameInfoClass = packages->FindClass("Botpack.DeathMatchPlus");
	if (!gameInfoClass)
		Exception::Throw("Could not find any gameinfo class!");
	LogMessage("Loaded level " + url.Map + " with game class " + gameInfoClass->Name.ToString());

	// Spawn GameInfo actor
	GameInfo = UObject::Cast<UGameInfo>(LevelPackage->NewObject("gameinfo", gameInfoClass, ObjectFlags::NoFlags));
	GameInfo->XLevel() = Level;
	GameInfo->Level() = LevelInfo;
	Level->Collision.AddToCollision(GameInfo);
	GameInfo->Tag() = gameInfoClass->Name;
	GameInfo->bTicked() = false;
	GameInfo->InitActorZone();
	GameInfo->Index = (int)Level->Actors.size();
	Level->Actors.push_back(GameInfo);

	LevelInfo->Game() = GameInfo;

	if (!LevelInfo->bBegunPlay())
	{
		LevelInfo->TimeSeconds() = 0.0f;
		LevelInfo->bBegunPlay() = true;

		std::string options = url.GetOptions();

		auto stringProp = GC::Alloc<UStringProperty>("", nullptr, ObjectFlags::NoFlags);
		std::string error;

		LevelInfo->bStartup() = true;
		CallEvent(GameInfo, EventName::InitGame, { ExpressionValue::StringValue(options), ExpressionValue::Variable(&error, stringProp) });
		if (!error.empty())
			Exception::Throw("InitGame failed: " + error);

		// Note: the events may spawn actors. We can't use iterators here.
		// Spawned objects must be initialized twice.
		// DX breaks if SetInitialState isn't called a second time on the spawned inventory after DeusExCarcass.PostBeginPlay finishes.
		for (size_t i = 0; i < Level->Actors.size(); i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], EventName::PreBeginPlay); }
		for (size_t i = 0; i < Level->Actors.size(); i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], EventName::BeginPlay); }
		for (size_t i = 0; i < Level->Actors.size(); i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], EventName::PostBeginPlay); }
		for (size_t i = 0; i < Level->Actors.size(); i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], EventName::SetInitialState); }

		if (engine->LaunchInfo.IsDeusEx())
		{
			for (size_t i = 0; i < Level->Actors.size(); i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], "PostPostBeginPlay"); }
		}

		for (size_t i = 0; i < Level->Actors.size(); i++) { if (Level->Actors[i]) Level->Actors[i]->InitBase(); }
		LevelInfo->bStartup() = false;
	}

	if (LevelInfo->Game())
		CallEvent(LevelInfo->Game(), "DetailChange", {});
}

void Engine::LoadFromSaveFile(const UnrealURL& url)
{
	runePendingButtons.clear();runePulsedButtons.clear();
	ClientTravelInfo.URL.Clear();

	if (Level)
		CallEvent(console, EventName::NotifyLevelChange);

	Package* savefilePackage = nullptr;
	uint32_t slotNum = 0;

	if (url.HasOption("load"))
	{
		slotNum = Convert::to_uint32(url.GetOption("load"));
		savefilePackage = packages->LoadSaveSlot(slotNum);
	}

	if (!savefilePackage)
		return;

	audiodev->StopSounds();
	UnloadMap();

	LevelPackage = savefilePackage;

	GetLevelInfoObject();

	// Same as LoadMap: these are session/engine identity, not save data, and must be
	// re-established on every load regardless of what the package/save file contains.
	LevelInfo->ComputerName() = "MyComputer";
	LevelInfo->HubStackLevel() = 0; // To do: handle level hubs
	LevelInfo->EngineVersion() = LaunchInfo.gameVersionString + " SE";
	if (LaunchInfo.ue1Version > 219)
		LevelInfo->MinNetVersion() = LaunchInfo.gameVersionString + " SE";
	LevelInfo->bHighDetailMode() = true;
	LevelInfo->NetMode() = 0; // NM_StandAlone
	LevelInfo->DefaultTexture() = engine->DefaultTexture;

	// LevelInfo->URL is not serialized, so it is
	// never restored by loading the save package and must be rebuilt
	std::string realMapName = packages->GetIniValue("user", "SaveGame", "MapName" + std::to_string(slotNum));
	if (LaunchInfo.IsRune() && commandline->HasArg("", "--save-directory"))
	{
		std::ifstream metadata(packages->GetSaveFolderPath()/("Save"+std::to_string(slotNum)+".mapname"));
		if (!std::getline(metadata,realMapName) || realMapName.empty()) Exception::Throw("Isolated save has no map metadata");
	}
	if (realMapName.empty())
		realMapName = LevelPackage->GetPackageName().ToString();
	LevelInfo->URL = UnrealURL(realMapName);

	GetLevelObject();

	LinkActorsToLevel();

	// engine->GameInfo is otherwise only assigned in LoadMap, so without this it keeps
	// pointing at the previous, now-unloaded level's GameInfo. LevelInfo->Game() is a normal
	// script property, so it round-trips through the save correctly; just re-point at it.
	GameInfo = UObject::Cast<UGameInfo>(LevelInfo->Game());
	if (!GameInfo)
		Exception::Throw("Save file has no GameInfo actor for " + LevelPackage->GetPackageName().ToString() + "!");
}

void Engine::PossessSavedPlayer()
{
	// Loading a save must not reuse LoginPlayer, because that always calls GameInfo.Login,
	// which always spawns a brand new pawn. The save package already contains the actual saved
	// pawn - deserialized with its real position, health and inventory - sitting in
	// Level->Actors. Find and possess that one directly instead.
	UPlayerPawn* pawn = nullptr;
	for (UActor* actor : Level->Actors)
	{
		UPlayerPawn* p = UObject::TryCast<UPlayerPawn>(actor);
		if (p && p->bIsPlayer())
		{
			pawn = p;
			break;
		}
	}

	if (!pawn)
		Exception::Throw("Save file has no player pawn for " + LevelPackage->GetPackageName().ToString() + "!");

	if (auto pawnExt = UObject::TryCast<UPlayerPawnExt>(pawn))
	{
		// FlagBase is Transient (not saved), so it needs the same reconstruction LoginPlayer
		// does for a fresh spawn.
		if (!pawnExt->FlagBase())
		{
			auto flagBaseCls = packages->FindClass("Extension.FlagBase");
			pawnExt->FlagBase() = UObject::Cast<UFlagBase>(packages->GetTransientPackage()->NewObject("FlagBase", flagBaseCls, ObjectFlags::Transient));
		}
	}

	viewport->Actor() = pawn;
	viewport->Actor()->Player() = viewport;
	// Rune Possess runs ClientRestart, resetting saved movement, animation and
	// script state. A saved pawn already owns those; only reconnect its viewport.
	if (!LaunchInfo.IsRune()) CallEvent(viewport->Actor(), EventName::Possess);

	if (render) render->OnMapLoaded();
    else Level->Light.OnMapLoaded(); // Dedicated authority has no renderer.
}

void Engine::SaveGameToSlot(int32_t slotNum, const std::string& saveDescription) const
{
#ifdef OPENRUNE_ENABLE_NETWORK
    if(NetworkClient)return; // Replicas can never write campaign save slots.
#endif
	if (slotNum < -1 || (!packages->IsDeusEx() && slotNum < 0))
		Exception::Throw("Invalid save slot: " + std::to_string(slotNum));

    RuneSpider::BeforeSave();
    RuneCorpse::BeforeSave();
    RunePhysics::BeforeSave();
    RuneSlam::BeforeSave();
	// First and foremost ensure the Save folder exists
	const auto saveFolderPath = packages->GetSaveFolderPath();
	if (!fs::exists(saveFolderPath))
		fs::create_directory(saveFolderPath);

	if (packages->IsDeusEx())
	{
		// Saving a game on Deus Ex does the following:
		// - Create a folder using the slotNum (e.g. 1 -> "Save0001")
		// - Save the level package using the name [MapName].dxs
		// - Save the associated DeusExSaveInfo class as SaveInfo.dxs within that same folder,
		// in which saveDescription parameter will be used in DeusExSaveInfo.Description
		auto slotNumStr = std::to_string(slotNum);
		slotNumStr.insert(0, 4 - slotNumStr.length(), '0'); // Pad it with 0s
		auto saveFolder = "Save" + slotNumStr;

		auto saveSlotFolder = saveFolderPath / saveFolder;
		if (!fs::exists(saveSlotFolder) || !fs::is_directory(saveSlotFolder))
			fs::create_directory(saveSlotFolder);

		auto levelName = Level->package->GetPackageName().ToString() + "." + packages->GetSaveExtension();
		auto saveInfoName = "SaveInfo." + packages->GetSaveExtension();
		auto saveFileFullPath = (saveSlotFolder / levelName).string();
		auto saveInfoFullPath = (saveSlotFolder / saveInfoName).string();
		LevelPackage->Save(Level, saveFileFullPath);

		dxSaveInfo->DirectoryIndex() = slotNum;
		dxSaveInfo->Description() = saveDescription;
		dxSaveInfo->MissionLocation() = DeusExLevelInfo ? DeusExLevelInfo->MissionLocation() : "";
		dxSaveInfo->MapName() = Level->package->GetPackageName().ToString();
		dxSaveInfo->UpdateTimeStamp();
		deusExPackage->Save(dxSaveInfo, saveInfoFullPath);
	}
	else
	{
		const std::string saveFileName = "Save" + std::to_string(slotNum) + "." + packages->GetSaveExtension();
		const std::string saveFileFullPath = (saveFolderPath / saveFileName).string();
		LevelPackage->Save(Level, saveFileFullPath);

		// The save package is later reloaded by its slot filename ("SaveN"), not by the original
		// map name, so record the real map name here for LoadFromSaveFile() to recover.
		std::string mapName = LevelInfo->URL.Map;
		if (mapName.empty()) mapName = Level->package->GetPackageName().ToString();
        // Preserve the opt-in campaign identity in existing map metadata. Old
        // plain-map saves remain valid; do not persist unrelated URL secrets.
        if(LaunchInfo.IsRune() && LevelInfo->URL.HasOption("openrunecharacter")) {
            mapName+="?OpenRuneCharacter="+LevelInfo->URL.GetOption("OpenRuneCharacter");
            mapName+="?OpenRuneSkin="+LevelInfo->URL.GetOption("OpenRuneSkin");
        }
		if (LaunchInfo.IsRune() && commandline->HasArg("", "--save-directory"))
		{
			// Keep validation checkpoints and their metadata out of normal slots.
			auto path=saveFolderPath/("Save"+std::to_string(slotNum)+".mapname");
			auto temporary=path; temporary+=".tmp";
			std::ofstream metadata(temporary); metadata<<mapName<<'\n'; metadata.close();
			if (!metadata) Exception::Throw("Could not write isolated save map metadata");
			fs::rename(temporary,path);
		}
		else
		{
			packages->SetIniValue("user", "SaveGame", "MapName" + std::to_string(slotNum), mapName);
			// Android can terminate a background process without engine shutdown.
			if (LaunchInfo.IsRune()) packages->SaveAllIniFiles();
		}
	}
}

std::map<std::string, std::string> Engine::CreateTravelInfo(bool transferItems)
{
	auto travelInfo = Level->TravelInfo;
	for (UActor* actor : Level->Actors)
	{
		UPlayerPawn* pawn = UObject::TryCast<UPlayerPawn>(actor);
		if (pawn && pawn->Player())
		{
			std::string playerName = engine->LaunchInfo.ue1Version > 219 ? pawn->PlayerReplicationInfo()->PlayerName() : std::string("Player"); // To do: how to get the travel player name?
			travelInfo[playerName] = ActorTravelInfo::Create(pawn, transferItems);
		}
	}
	return travelInfo;
}

void Engine::LoginPlayer()
{
    LoginPlayerConnection(viewport, LevelInfo->URL);
    if (render) render->OnMapLoaded();
    else Level->Light.OnMapLoaded();
}

UPlayerPawn* Engine::LoginNetworkPlayer(const std::string& name,int team,int character,int skin)
{
    if (!NetworkAuthority || !LevelInfo || name.empty() || name.size()>24 ||
        !std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-' || c==' ';}))
        Exception::Throw("Invalid network player login");
    UnrealURL url=LevelInfo->URL;
    for(auto it=url.Options.begin();it!=url.Options.end();)
    {
        if(NameString(it->substr(0,it->find('=')))=="Name" || (team>=0 && NameString(it->substr(0,it->find('=')))=="Team")) it=url.Options.erase(it);
        else ++it;
    }
    if(character<0 || character>=int(openrune::Characters.size()) || skin<0 || skin>=openrune::Characters[character].skins)
        Exception::Throw("Invalid network character");
    url.AddOrReplaceOption(std::string("Class=")+openrune::Characters[character].className);
    url.AddOrReplaceOption("Skin="+std::to_string(skin));
    url.Options.push_back("Name="+name);
    if(team>=0)url.Options.push_back("Team="+std::to_string(team));
    auto* connection=UObject::Cast<UPlayer>(packages->GetTransientPackage()->NewObject(
        "NetworkPlayer"+std::to_string(Level->Actors.size()),packages->GetPackage("Engine")->GetClass("Player"),ObjectFlags::Transient));
    return LoginPlayerConnection(connection,url);
}

UPlayerPawn* Engine::LoginPlayerConnection(UPlayer* connection, const UnrealURL& url)
{
	std::map<std::string, std::string> travelInfo = Level->TravelInfo;

	auto stringProp = GC::Alloc<UStringProperty>("", nullptr, ObjectFlags::NoFlags);
	std::string error, failcode;

	std::string portal = url.GetPortal();
    UnrealURL loginURL=url;
    // The story deliberately chooses Town/Shipwreck Ragnar. Only an explicit
    // campaign override bypasses that choice; the URL carries it through saves/travel.
    UClass* storyDefault=nullptr;
    const bool customStory=LaunchInfo.IsRune() && LevelInfo->Game()->IsA("RuneSinglePlayer") && url.HasOption("openrunecharacter");
    if(customStory) {
        const auto raw=url.GetOption("OpenRuneCharacter"), rawSkin=url.GetOption("OpenRuneSkin");
        const auto index=std::stoi(raw),skin=rawSkin.empty()?0:std::stoi(rawSkin);
        if(index<0 || index>=int(openrune::Characters.size()) || skin<0 || skin>=openrune::Characters[index].skins)
            Exception::Throw("Invalid saved campaign character");
        loginURL.AddOrReplaceOption(std::string("Class=")+openrune::Characters[index].className);
        loginURL.AddOrReplaceOption("Skin="+std::to_string(skin));
        storyDefault=UObject::TryCast<UClass>(LevelInfo->Game()->GetUObject("DefaultPlayerClass"));
    }
	std::string options = loginURL.GetOptions();

	std::string playerPawnClass = loginURL.GetOption("Class");
	if (playerPawnClass.empty())
		playerPawnClass = packages->GetIniValue("system", "URL", "Class");
	UClass* pawnClass = packages->FindClass(playerPawnClass);

	// Perform PreLogin check (used for early rejection in network games)
	CallEvent(LevelInfo->Game(), EventName::PreLogin, {
		ExpressionValue::StringValue(options),
		ExpressionValue::Variable(&error, stringProp),
		ExpressionValue::Variable(&failcode, stringProp),
		});
	if (!error.empty() || !failcode.empty())
		Exception::Throw("GameInfo prelogin failed: " + error + " (" + failcode + ")");

    // Scope the script override to Login, including exceptions.
    struct RestoreStoryClass {
        UObject* game;UClass* saved;bool active;
        ~RestoreStoryClass(){if(active)game->SetObject("DefaultPlayerClass",saved);}
    } restore{LevelInfo->Game(),storyDefault,customStory};
    if(customStory)LevelInfo->Game()->SetObject("DefaultPlayerClass",pawnClass);
	// Create a pawn for this specific player connection
	size_t numActors = Level->Actors.size();
	UPlayerPawn* pawn = UObject::Cast<UPlayerPawn>(CallEvent(LevelInfo->Game(), EventName::Login, {
		ExpressionValue::StringValue(portal),
		ExpressionValue::StringValue(options),
		ExpressionValue::Variable(&error, stringProp),
		ExpressionValue::ObjectValue(pawnClass)
		}).ToObject());
	if (!pawn || !error.empty())
		Exception::Throw("GameInfo login failed: " + error);
	LogMessage("Logged in pawn " + pawn->Class->Name.ToString());
	bool actorActuallySpawned = Level->Actors.size() != numActors;

	pawn->LoadProperties();

	if (auto pawnExt = UObject::TryCast<UPlayerPawnExt>(pawn))
	{
		// Unclear if this is how DeusEx spawned this object
		if (!pawnExt->FlagBase())
		{
			auto flagBaseCls = packages->FindClass("Extension.FlagBase");
			pawnExt->FlagBase() = UObject::Cast<UFlagBase>(packages->GetTransientPackage()->NewObject("FlagBase", flagBaseCls, ObjectFlags::Transient));
		}
	}

	// Retain distinct ownership even without a local viewport.
    connection->Actor() = pawn;
    pawn->Player() = connection;
    CallEvent(pawn, EventName::Possess);

	// Transfer travel actors to the new map

	CallEvent(pawn, EventName::TravelPreAccept);

	Array<UObject*> acceptedObjects;
	if (actorActuallySpawned && ClientTravelInfo.TravelType == ETravelType::TRAVEL_Relative)
	{
		std::string playerName = url.GetOption("Name");
		if (playerName.empty())
			playerName = packages->GetIniValue("system", "URL", "Name");

		auto it = travelInfo.find(playerName);
		if (!playerName.empty() && it != travelInfo.end())
		{
			acceptedObjects = ActorTravelInfo::Accept(pawn, it->second);
		}
		else
		{
			if (travelInfo.empty())
				LogMessage("Skipping travel transfer. No travel data");
			else if (playerName.empty())
				LogMessage("Skipping travel transfer. Player name is empty");
			else
				LogMessage("Skipping travel transfer. Player '" + playerName + "' not found in travel info");
		}
	}

	for (UObject* object : acceptedObjects)
		CallEvent(object, EventName::TravelPreAccept);

	CallEvent(LevelInfo->Game(), EventName::AcceptInventory, { ExpressionValue::ObjectValue(pawn) });

	for (UObject* object : acceptedObjects)
		CallEvent(object, EventName::TravelPostAccept);

	CallEvent(pawn, EventName::TravelPostAccept);
	CallEvent(LevelInfo->Game(), EventName::PostLogin, { ExpressionValue::ObjectValue(pawn) });

    return pawn;
}

UZoneInfo* Engine::GetZoneActor(int zoneIndex)
{
	if (auto zone = UObject::TryCast<UZoneInfo>(Level->Model->Zones[zoneIndex].ZoneActor))
		return zone;
	else
		return LevelInfo;
}

UObject* Engine::FindObject(NameString name, NameString className)
{
	for (auto actor : Level->Actors)
	{
		if (actor && actor->Name == name && UObject::GetUClassFullName(actor) == className)
			return actor;
	}

	return nullptr;
}

float Engine::CalcTimeElapsed()
{
	using namespace std::chrono;

	uint64_t currentTime = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
	if (lastTime == 0)
		lastTime = currentTime;

	uint64_t deltaTime = currentTime - lastTime;
	lastTime = currentTime;
	return clamp(deltaTime / 1'000'000.0f, 0.0f, 1.0f);
}

std::string Engine::ParseClassName(std::string className)
{
	// Workaround for broken unrealscript code referencing windrv.windowsclient directly
	if (className == "windrv.windowsclient")
	{
		className = "ini:Engine.ViewportManager";
	}

	if (className.size() < 4 || className.substr(0, 4) != "ini:")
		return className;

	size_t pos = className.find_last_of('.');
	if (pos == std::string::npos)
		Exception::Throw("Parse error");

	NameString sectionName = className.substr(4, pos - 4);
	NameString keyName = className.substr(pos + 1);

	// Override the ini file for things that are internal in Surreal Engine
	if (sectionName == "Engine.Engine")
	{
		if (keyName == "GameRenderDevice" || keyName == "WindowedRenderDevice")
		{
			return renderdev->Class;
		}
		else if (keyName == "AudioDevice")
		{
			return audiodev->Class;
		}
		else if (keyName == "NetworkDevice")
		{
			return netdev->Class;
		}
		else if (keyName == "ViewportManager")
		{
			return client->Class;
		}
	}

	return packages->GetIniValue("system", sectionName, keyName);
}

std::string Engine::ConsoleCommand(UObject* context, const std::string& commandline, BitfieldBool& found)
{
	found = false;

	Array<std::string> args = GetArgs(commandline);
	if (args.empty())
	{
		return {};
	}

	std::string command = args[0];
	for (char& c : command) c = std::tolower(c);

	found = true;
	if (command == "exit" || command == "quit")
	{
		quit = true;
	}
    else if(command=="gore" && args.size()==2 && LaunchInfo.IsRune()) {
        if(args[1]!="0" && args[1]!="1")return "Use gore 0 (Original) or gore 1 (Enhanced).";
        RuneGore::Enabled=args[1]=="1";
        return RuneGore::Enabled?"Enhanced gore presentation":"Original gore presentation";
    }
    else if((command=="materiallighting" || command=="environmentlighting") && args.size()==2 && LaunchInfo.IsRune()) {
        if(args[1]!="0" && args[1]!="1")return "Use 0 (off) or 1 (on).";
        int bit=command=="materiallighting"?65536:131072;
        int world=std::max(VisualComparison::AppliedWorldFeatures.load(),0);
        VisualComparison::RequestedWorldFeatures=(world&~bit)|(args[1]=="1"?bit:0);
        return "Lighting choice updated; requires Remastered lighting.";
    }
	else if (command == "timedemo" && args.size() == 2)
	{
		render->ShowTimedemoStats = args[1] == "1";
	}
	else if (command == "stat" && args.size() == 2)
	{
		render->ShowRenderStats = 0;

		if (args[1] == "render")
			render->ShowRenderStats = 1;
	}
	else if (command == "collisiondebug" && args.size() == 2)
	{
		render->ShowCollisionDebug = args[1] == "1";
	}
	else if (command == "dxwindowdebug" && LaunchInfo.IsDeusEx())
	{
		m_DrawDebugDXWindowHierarchy = !m_DrawDebugDXWindowHierarchy;
	}
	else if (command == "showlog")
	{
		//Frame::ShowDebuggerWindow();
	}
	/*else if (command == "playsong")
	{
		auto music = LevelInfo->Song();
		if (music)
			audio->PlayMusic(AudioSource::CreateMod(music->Data, true, 0, LevelInfo->SongSection()));
	}
	else if (command == "stopsong")
	{
		audio->PlayMusic(nullptr);
	}*/
	else if (command == "getres")
	{
		if (LaunchInfo.IsHarryPotter1()) // FEOptionsPage.IsSupportedResolution is so sad :)
			return "1024x768";
		return window->GetAvailableResolutions();
	}
	else if (command == "getcolordepths")
	{
		return "32 16";
	}
	else if (command == "getcurrentres")
	{
		int width = window->GetPixelWidth();
		int height = window->GetPixelHeight();

		return std::to_string(width) + "x" + std::to_string(height);
	}
	else if (command == "getcurrentcolordepth")
	{
		return "32";
	}
	else if (command == "getping")
	{
		return "0";
	}
	else if (command == "getloss")
	{
		return "0";
	}
	else if (command == "keyname" && args.size() == 2)
	{
		uint8_t index = Convert::to_uint8(args[1]);
		return keynames[index];
	}
	else if (command == "keybinding" && args.size() == 2)
	{
		const std::string& name = args[1];
		return keybindings[name];
	}
	else if ((command == "open" || command == "start") && args.size() == 2)
	{
		const std::string& maparg = args[1];

		UnrealURL url(maparg);

		for (auto& map : packages->GetMaps())
		{
			std::string mapname = fs::path(map).stem().string();

			if (StrTools::equals_ignore_case(mapname, url.Map))
			{
				ClientTravel(url.ToString(), ETravelType::TRAVEL_Absolute, false);
				return {};
			}	
		}

		LogMessage("Couldn't find map " + maparg);
	}
	else if (command == "switchlevel" && args.size() == 2)
	{
		// This works like open/start, but keeps the difficulty level
		// As well as the game type
		const std::string& maparg = args[1];

		UnrealURL url(maparg);

		// First check if the provided URL has a difficulty option
		std::string difficulty = url.GetOption("difficulty");

		// If there isn't, try to get it from the current level's options
		if (difficulty.empty())
			difficulty = LevelInfo->URL.GetOption("difficulty");

		// If there still isn't, refer to GameInfo for difficulty
		if (difficulty.empty())
			difficulty = std::to_string(GameInfo->Difficulty());

		if (!difficulty.empty())
			url.AddOrReplaceOption("difficulty=" + difficulty);

		for (auto& map : packages->GetMaps())
		{
			std::string mapname = fs::path(map).stem().string();

			if (StrTools::equals_ignore_case(mapname, url.Map))
			{
				LevelInfo->NextURL() = url.ToString();
				return {};
			}
		}

		LogMessage("Couldn't find map " + maparg);
	}
	else if (command == "savegame" && (args.size() == 2 || args.size() == 3))
	{

		int32_t slotNum;
		// slotNum not being parsable shouldn't cause a crash
		try
		{
			slotNum = Convert::to_int32(args[1]);
		}
		catch (...)
		{
			return {};
		}

		SaveGameInfo.SaveGameSlot = slotNum;

		if (args.size() == 3)
			SaveGameInfo.SaveGameDescription = args[2];

		// SaveGameToSlot(slotNum, "");

		//LogMessage("SaveGame command not fully implemented yet!");
		return {};
	}
	else if (command == "get" && args.size() == 3)
	{
		NameString className = ParseClassName(args[1]);
		NameString propertyName = args[2];

		UClass* cls = packages->FindClass(className);
		if (!cls)
		{
			LogMessage("Could not find class '" + className.ToString() + "': " + commandline);
			return {};
		}

		if (className == renderdev->Class)
		{
			return renderdev->GetPropertyAsString(propertyName);
		}
		else if (className == audiodev->Class)
		{
			return audiodev->GetPropertyAsString(propertyName);
		}
		else if (className == netdev->Class)
		{
			return netdev->GetPropertyAsString(propertyName);
		}
		else if (className == client->Class)
		{
			return client->GetPropertyAsString(propertyName);
		}
		else
		{
			try
			{
				return cls->GetPropertyAsString(propertyName);
			}
			catch (const std::exception&)
			{
				LogMessage("Could not get property '" + propertyName.ToString() + "': " + commandline);
				return {};
			}
		}
	}
	else if (command == "set" && args.size() == 4)
	{
		NameString className = ParseClassName(args[1]);
		NameString propertyName = args[2];
		std::string value = args[3];

		// Special input setting handling
		if (className == "input")
		{
			keybindings[propertyName.ToString()] = value;
			packages->SetIniValue("user", "Engine.Input", propertyName, value);
			return {};
		}

		if (LaunchInfo.IsDeusEx() && className == "inputext")
		{
			// DeusEx version of input handling. How does this differ from the other one?
			keybindings[propertyName.ToString()] = value;
			packages->SetIniValue("user", "Engine.Input", propertyName, value);
			return {};
		}

		UClass* cls = packages->FindClass(className);
		if (!cls)
		{
			LogMessage("Could not find class '" + className.ToString() + "': " + commandline);
			return {};
		}

		if (className == renderdev->Class)
		{
			renderdev->SetPropertyFromString(propertyName, value);
		}
		else if (className == audiodev->Class)
		{
			audiodev->SetPropertyFromString(propertyName, value);
		}
		else if (className == netdev->Class)
		{
			netdev->SetPropertyFromString(propertyName, value);
		}
		else if (className == client->Class)
		{
			client->SetPropertyFromString(propertyName, value);
		}
		else
		{
			try
			{
				cls->SetPropertyFromString(propertyName, value);
			}
			catch (const std::exception&)
			{
				LogMessage("Could not set property '" + propertyName.ToString() + "': " + commandline);
			}
			return {};
		}
	}
	else if (command == "setres" && args.size() == 2)
	{
		window->SetResolution(args[1]);
	}
	else if (command == "togglefullscreen")
	{
		bool isFullscreen = window->IsFullscreen();

		// Get the resolution to SWITCH TO
		int width = isFullscreen ? client->WindowedViewportX : client->FullscreenViewportX;
		int height = isFullscreen ? client->WindowedViewportY : client->FullscreenViewportY;

		Size resolution;
		resolution.width = width;
		resolution.height = height;

		window->ToggleWindowFullscreen(resolution);
		viewport->SetViewportRect(0, 0, width, height);

		return {};
	}
	else if (command == "prsq")
	{
		// Klingon Honor Guard: CD check
		// "mpgameplay"
		// "mpinstall"
		return "mpgameplay";
	}
	else if (command == "os")
	{
		// 227 Seems to have this command
#ifdef WIN32
		return "Windows";
#elif __APPLE__
		return "macOS";
#else
		return "Linux"; // With apologies to BSD, Haiku and others...
#endif
	}
	else if (command == "getsplash")
	{
		return khgSplashScreen ? "true" : "false";
	}
	else if (command == "setsplash")
	{
		khgSplashScreen = true;
		return {};
	}
	else if (command == "playavi")
	{
		PlayAVI(args);
		return {};
	}
	else if (command == "flush")
	{
		engine->render->Device->Flush(1);
		return {};
	}
	else
	{
		if (!ExecCommand(args))
		{
			LogMessage("Unknown command: " + commandline);
			found = false;
		}
	}
	return {};
}

Array<std::string> Engine::GetArgs(const std::string& commandline)
{
	Array<std::string> args;
	size_t i = 0;
	while (i < commandline.size())
	{
		size_t j = commandline.find_first_not_of(" \t", i);
		if (j == std::string::npos)
			break;
		i = j;
		j = commandline.find_first_of(" \t", i);
		if (j == std::string::npos)
			j = commandline.size();
		if (j > i)
			args.push_back(commandline.substr(i, j - i));
		i = j;
	}
	return args;
}

Array<std::string> Engine::GetSubcommands(const std::string& command)
{
	Array<std::string> subcommands;
	size_t pos = 0;
	while (pos < command.size())
	{
		size_t endpos = command.find('|', pos);
		if (endpos == std::string::npos)
			endpos = command.size();

		std::string subcommand = command.substr(pos, endpos - pos);
		if (!subcommand.empty())
			subcommands.push_back(subcommand);
		pos = endpos + 1;
	}
	return subcommands;
}

void Engine::LoadEngineSettings()
{
	if (packages->MissingSESystemIni())
	{
		client->LoadProperties("WinDrv.WindowsClient");
		audiodev->LoadProperties("Galaxy.GalaxyAudioSubsystem");
		renderdev->LoadProperties("D3DDrv.Direct3DRenderDevice");
	}
	else
	{
		client->LoadProperties();
		audiodev->LoadProperties();
		renderdev->LoadProperties();
	}

#ifdef WIN32
	windowingSystemName = packages->GetIniValue("System", "Engine.SurrealWindowSystem", "WindowSystem", "Win32");
#else
	windowingSystemName = packages->GetIniValue("System", "Engine.SurrealWindowSystem", "WindowSystem", "SDL2");
#endif
}

void Engine::LoadKeybindings()
{
	for (int i = 0; i < 256; i++)
	{
		std::string keyname = keynames[i];
		keybindings[keyname] = packages->GetIniValue("user", "Engine.Input", keyname);
	}

	for (int i = 0; i < 40; i++)
	{
		std::string alias = packages->GetIniValue("user", "Engine.Input", "Aliases[" + std::to_string(i) + "]");

		// Total trash parsing, but it will do for the aliases I have! Feel free to improve it!
		std::string commandStart = "(Command=\"";
		std::string commandSplit = "\",Alias=";
		std::string commandEnd = ")";
		if (alias.size() > commandStart.size() + commandSplit.size() + commandEnd.size())
		{
			size_t pos = alias.find(commandSplit, commandStart.size());
			if (pos != std::string::npos)
			{
				size_t pos2 = alias.find(commandEnd, pos);
				if (pos2 != std::string::npos)
				{
					std::string aliasCommand = alias.substr(commandStart.size(), pos - commandStart.size());
					std::string aliasName = alias.substr(pos + commandSplit.size(), pos2 - pos - commandSplit.size());
					if (!aliasName.empty() && aliasName != "None")
						inputAliases[aliasName] = aliasCommand;
				}
			}
		}
	}
}

void Engine::UpdateInput(float timeElapsed)
{
	if (timeElapsed <= 0.0f)
		return;

	// A tap gets one simulation update, without delaying press delivery or
	// keeping its button held on subsequent updates.
	if(viewport->Actor())for(const auto& name:runePulsedButtons)
		if(!activeInputButtons.count(name))viewport->Actor()->SetBool(name,false);
	runePulsedButtons.clear();
	TickWindow();
	if (tickDebugger)
		tickDebugger();

	if (!viewport->Actor())
		return;

	for (auto& it : activeInputButtons)
		viewport->Actor()->SetBool(it.first, true);
	for(const auto& name:runePendingButtons) {
		viewport->Actor()->SetBool(name,true);
		if(!activeInputButtons.count(name))runePulsedButtons.insert(name);
	}
	runePendingButtons.clear();
	for (auto& it : activeInputAxes)
	{
		if (it.first == "aMouseX" || it.first == "aMouseY")
		{
			viewport->Actor()->SetFloat(it.first, it.second.Value / (timeElapsed * 150.0f));
		}
		else
		{
			viewport->Actor()->SetFloat(it.first, it.second.Value);
		}
	}
#ifdef __ANDROID__
    if (LaunchInfo.IsRune())
    {
        const auto pad = RuneMenu::Open.load() ? RuneController::Axes{} : RuneController::Get();
        auto base = [&](const char* name) { auto it=activeInputAxes.find(name); return it==activeInputAxes.end()?0.0f:it->second.Value; };
        viewport->Actor()->SetFloat("aStrafe", base("aStrafe") + pad.strafe*6000.0f);
        viewport->Actor()->SetFloat("aBaseY", base("aBaseY") + pad.forward*6000.0f);
        // Rune scales look by 0.24, then integrates with DeltaTime.
        // 4096 gives about 173 degrees/second at the normal sensitivity.
        viewport->Actor()->SetFloat("aTurn", viewport->Actor()->GetFloat("aTurn") + pad.yaw*4096.0f);
        viewport->Actor()->SetFloat("aLookUp", viewport->Actor()->GetFloat("aLookUp") + pad.pitch*4096.0f);
    }
#endif
}

void Engine::OpenWindow()
{
	if (!window)
		window = GameWindow::Create(this);

	int width = client->StartupFullscreen ? client->FullscreenViewportX : client->WindowedViewportX;
	int height = client->StartupFullscreen ? client->FullscreenViewportY : client->WindowedViewportY;
	bool fullscreen = client->StartupFullscreen;

	std::string versionString = !LaunchInfo.gameVersionString.empty() ? " (v" + LaunchInfo.gameVersionString + ")" : "";

	window->SetWindowTitle(LaunchInfo.IsRune() ? "Open Rune" + versionString : LaunchInfo.gameName + versionString + " - Surreal Engine");
	window->SetFrameGeometry(Rect::xywh(0.0, 0.0, width, height));
	viewport->SetViewportRect(0, 0, width, height);

	if (fullscreen)
		window->ShowFullscreen();
	else
		window->ShowNormal();
}

void Engine::CloseWindow()
{
	window.reset();
}

void Engine::TickWindow()
{
    RuneDesktopMenu::Tick(*this);
    if(LaunchInfo.IsRune())RuneCharacterPreview::Tick(*this);
	if (window && engine->LaunchInfo.ue1Version > 219)
	{
		if (viewport->bShowWindowsMouse() && viewport->bWindowsMouseAvailable())
			window->UnlockCursor();
		else
			window->LockCursor();
	}

	InputEvent(IK_MouseX, IST_Axis, 0);
	InputEvent(IK_MouseY, IST_Axis, 0);

	GameWindow::ProcessEvents();

	if (MouseMoveX != 0 || MouseMoveY != 0)
	{
		int dx = MouseMoveX;
		int dy = MouseMoveY;
		MouseMoveX = 0;
		MouseMoveY = 0;

		// Send to input subsystem.
		if (dx)
			InputEvent(IK_MouseX, IST_Axis, dx);
		if (dy)
			InputEvent(IK_MouseY, IST_Axis, -dy);
	}
}

void Engine::OnWindowPaint()
{
}

void Engine::OnWindowMouseMove(const Point& pos)
{
    if(RuneDesktopMenu::Mouse(*this,pos,0))return;
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseMove(pos))
		return;

	if (engine->LaunchInfo.ue1Version > 219)
	{
		viewport->WindowsMouseX() = (float)(pos.x * window->GetDpiScale());
		viewport->WindowsMouseY() = (float)(pos.y * window->GetDpiScale());
	}
}

void Engine::OnWindowMouseDown(const Point& pos, EInputKey key)
{
    if(RuneDesktopMenu::Mouse(*this,pos,1,key))return;
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseDown(pos, key))
		return;

	InputEvent(key, IST_Press);
}

void Engine::OnWindowMouseDoubleclick(const Point& pos, EInputKey key)
{
    if(RuneDesktopMenu::Mouse(*this,pos,1,key))return;
	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseDoubleclick(pos, key))
		return;

	// Double click event sequence is: mouse down, up, doubleclick, up
	// Since we don't have double click events in the UE1 event system, the second click
	// should be treated as a normal mouse down event.
	InputEvent(key, IST_Press);
}

void Engine::OnWindowMouseUp(const Point& pos, EInputKey key)
{
    if(RuneDesktopMenu::Mouse(*this,pos,2,key))return;
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseUp(pos, key))
		return;

	InputEvent(key, IST_Release);
}

void Engine::OnWindowMouseWheel(const Point& pos, EInputKey key)
{
    if(RuneDesktopMenu::Mouse(*this,pos,3,key))return;
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseWheel(pos, key))
		return;

	InputEvent(key, IST_Press);
	InputEvent(key, IST_Release);
}

void Engine::OnWindowRawMouseMove(int dx, int dy)
{
    if(RuneDesktopMenu::Enabled(*this)) {
        if(RuneMenu::Open)return;
        dx=(int)std::round(dx*RuneDesktopMenu::LookScale());
        dy=(int)std::round(dy*RuneDesktopMenu::LookScale()*(RuneDesktopMenu::InvertLook()?-1:1));
    }
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowRawMouseMove(dx, dy))
		return;

	MouseMoveX += dx;
	MouseMoveY += dy;
}

void Engine::OnWindowKeyChar(std::string chars)
{
    if(RuneDesktopMenu::Text(*this,chars))return;
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowKeyChar(chars))
		return;

	Key(chars);
}

void Engine::OnWindowKeyDown(EInputKey key)
{
    if(RuneDesktopMenu::Key(*this,key,true))return;
#ifdef OPENRUNE_ENABLE_NETWORK
    if(NetworkClient){InputEvent(key,IST_Press);return;}
#endif
	if (playingAvi)
	{
		if (key == EInputKey::IK_Escape)
			skipAvi = true;
		return;
	}

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowKeyDown(key))
		return;

    if(LaunchInfo.IsRune() && key==EInputKey::IK_F) {
        if(!RuneMenu::Open.load() && viewport && viewport->Actor())RuneKick::Start(viewport->Actor());
        return;
    }
	InputEvent(key, IST_Press);
}

void Engine::OnWindowKeyUp(EInputKey key)
{
    if(RuneDesktopMenu::Key(*this,key,false))return;
#ifdef OPENRUNE_ENABLE_NETWORK
    if(NetworkClient){InputEvent(key,IST_Release);return;}
#endif
    if(LaunchInfo.IsRune() && key==EInputKey::IK_F)return;
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowKeyUp(key))
		return;

	InputEvent(key, IST_Release);
}

void Engine::OnWindowGeometryChanged()
{
}

void Engine::OnWindowClose()
{
	quit = true;
}

void Engine::OnWindowActivated()
{
	//SetPause(false);
}

void Engine::OnWindowDeactivated()
{
#ifdef OPENRUNE_ENABLE_NETWORK
    if(NetworkFocusLost)NetworkFocusLost();
#endif
	//SetPause(true);
}

void Engine::OnWindowDpiScaleChanged()
{
}

void Engine::LockCursor()
{
	if (window)
		window->LockCursor();
}

void Engine::UnlockCursor()
{
	if (window)
		window->UnlockCursor();
}

void Engine::Key(std::string key)
{
#ifdef OPENRUNE_ENABLE_NETWORK
    if(NetworkClient)return;
#endif
	if (Frame::RunState != FrameRunState::Running || playingAvi)
		return;

	if (LaunchInfo.IsDeusEx() && key == "~" && window->GetKeyState(IK_Shift))
	{
		// Did they REALLY hack UE1 to do something as lame as this??
		console->GotoState("Typing", {});
	}

	for (char c : key)
	{
		CallEvent(console, EventName::KeyType, { ExpressionValue::ByteValue(c) });
	}
}

void Engine::InputEvent(EInputKey key, EInputType type, int delta)
{
#ifdef OPENRUNE_ENABLE_NETWORK
    if(NetworkInput){NetworkInput(key,type,delta);return;}
#endif
	if (RuneMenu::Open.load()) return;
	if (Frame::RunState != FrameRunState::Running || playingAvi)
		return;

	bool handled = CallEvent(console, EventName::KeyEvent, { ExpressionValue::ByteValue(key), ExpressionValue::ByteValue(type), ExpressionValue::FloatValue((float)delta) }).ToBool();
	
	if (!handled)
	{
		if ((type == EInputType::IST_Press || type == EInputType::IST_Axis) && key >= 0 && key < 256)
		{
			if (type == EInputType::IST_Press)
				delta = 20;
			else
				delta *= 16;

			// SDL reports left Control separately; Rune's default Use binding is
			// the legacy Ctrl key. Preserve explicit left/right bindings (Block).
			std::string binding = keybindings[keynames[key]];
			if (LaunchInfo.IsRune() && key == IK_LControl && binding.empty())
				binding = keybindings["Ctrl"];
			if (LaunchInfo.IsRune() && commandline->HasArg("", "--trace-rune-combat") && type == IST_Press)
				LogMessage("RUNE_INPUT key="+std::string(keynames[key])+" binding="+binding);
			for (const std::string& command : GetSubcommands(binding))
			{
				auto it = inputAliases.find(command);
				if (it != inputAliases.end())
				{
					InputCommand(it->second, key, delta);
				}
				else
				{
					InputCommand(command, key, delta);
				}
			}
		}
		else if (type == EInputType::IST_Release)
		{
			for (auto it = activeInputButtons.begin(); it != activeInputButtons.end();)
			{
				if (it->second == key)
				{
					if (viewport->Actor())
						viewport->Actor()->SetBool(it->first, false);
					it = activeInputButtons.erase(it);
				}
				else
				{
					++it;
				}
			}

			for (auto it = activeInputAxes.begin(); it != activeInputAxes.end();)
			{
				if (it->second.Key == key)
				{
					if (viewport->Actor())
						viewport->Actor()->SetFloat(it->first, 0.0f);
					it = activeInputAxes.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
	}
}

bool Engine::ExecCommand(const Array<std::string>& args)
{
	for (UObject* target : { static_cast<UObject*>(viewport->Actor()), static_cast<UObject*>(console) })
	{
		if (!target)
			continue;

		UFunction* func = FindEventFunction(target, args[0]);
		if (func && AllFlags(func->FuncFlags, FunctionFlags::Exec))
		{
			Array<ExpressionValue> vmArgs;
			int argindex = 0;
			for (UField* field = func->Children; field != nullptr; field = field->Next)
			{
				UProperty* prop = UObject::TryCast<UProperty>(field);
				if (!prop)
					continue;

				if (AllFlags(prop->PropFlags, PropertyFlags::ReturnParm))
					continue;

				if (!AllFlags(prop->PropFlags, PropertyFlags::Parm))
					continue;

				if (argindex + 1 < args.size())
				{
					const std::string& arg = args[1 + argindex];
					switch (prop->ValueType)
					{
					case ExpressionValueType::Nothing: vmArgs.push_back(ExpressionValue::NothingValue()); break;
					case ExpressionValueType::ValueByte: vmArgs.push_back(ExpressionValue::ByteValue(std::atoi(arg.c_str()))); break;
					case ExpressionValueType::ValueInt: vmArgs.push_back(ExpressionValue::IntValue(std::atoi(arg.c_str()))); break;
					case ExpressionValueType::ValueBool: vmArgs.push_back(ExpressionValue::BoolValue(arg == "1" || arg == "true")); break;
					case ExpressionValueType::ValueFloat: vmArgs.push_back(ExpressionValue::FloatValue((float)std::atof(arg.c_str()))); break;
					case ExpressionValueType::ValueString: vmArgs.push_back(ExpressionValue::StringValue(arg)); break;
					case ExpressionValueType::ValueName: vmArgs.push_back(ExpressionValue::NameValue(arg)); break;
					default: LogMessage("Unsupported value type found in Engine.ExecCommand"); return false;
					}
				}
				else if (AllFlags(prop->PropFlags, PropertyFlags::OptionalParm))
				{
					vmArgs.push_back(ExpressionValue::NothingValue());
				}
				else
				{
					switch (prop->ValueType)
					{
					case ExpressionValueType::Nothing: vmArgs.push_back(ExpressionValue::NothingValue()); break;
					case ExpressionValueType::ValueByte: vmArgs.push_back(ExpressionValue::ByteValue(0)); break;
					case ExpressionValueType::ValueInt: vmArgs.push_back(ExpressionValue::IntValue(0)); break;
					case ExpressionValueType::ValueBool: vmArgs.push_back(ExpressionValue::BoolValue(false)); break;
					case ExpressionValueType::ValueFloat: vmArgs.push_back(ExpressionValue::FloatValue(0.0f)); break;
					case ExpressionValueType::ValueString: vmArgs.push_back(ExpressionValue::StringValue({})); break;
					case ExpressionValueType::ValueName: vmArgs.push_back(ExpressionValue::NameValue({})); break;
					default: LogMessage("Unsupported value type found in Engine.ExecCommand"); return false;
					}
				}
				argindex++;
			}

			CallEvent(target, func->Name, vmArgs);
			return true;
		}
	}

	return false;
}

void Engine::InputCommand(const std::string& commands, EInputKey key, int delta)
{
	for (const std::string& commandline : GetSubcommands(commands))
	{
		Array<std::string> args = GetArgs(commandline);
		if (!args.empty())
		{
			std::string command = args[0];
			for (char& c : command) c = std::tolower(c);

			if (command == "button" && args.size() == 2)
			{
				if(LaunchInfo.IsRune() && args[1]=="bFire" && !activeInputButtons.count(args[1]) && activeInputButtons.count("bAltFire") && RuneBloodlust::Charge(viewport->Actor()))continue;
                if(LaunchInfo.IsRune() && !activeInputButtons.count(args[1]))runePendingButtons.insert(args[1]);
				if(LaunchInfo.IsRune() && args[1]=="bAltFire" && activeInputButtons.find(args[1])==activeInputButtons.end())
                    RuneBash::BlockPressed(viewport->Actor());
                activeInputButtons[args[1]] = key;
			}
			else if (command == "axis" && args.size() == 3)
			{
				float speed = 1.0f;
				if (args[2].size() > 6 && args[2].substr(0, 6) == "Speed=")
					speed = (float)std::atof(args[2].substr(6).c_str());
				activeInputAxes[args[1]] = { speed * delta, key };
			}
			else
			{
				ExecCommand(args);
			}
		}
	}
}

void Engine::SetPause(bool value)
{
	m_GamePaused = value;
}

void Engine::LogGamePackageSHA1Sums() const
{
	auto systemPath = packages->GetSystemFolderPath();

	LogMessage("SHA1Sums of some system files:");
	LogMessage("Core.u: " + SHA1Sum::of_file(systemPath / "Core.u"));
	LogMessage("Engine.u: " + SHA1Sum::of_file(systemPath / "Engine.u"));

	if (packages->IsUnrealTournament())
	{
		LogMessage("Botpack.u: " + SHA1Sum::of_file(systemPath / "Botpack.u"));
		LogMessage("UnrealI.u: " + SHA1Sum::of_file(systemPath / "UnrealI.u"));
		LogMessage("UnrealShare.u: " + SHA1Sum::of_file(systemPath / "UnrealShare.u"));
	}
	else if (packages->IsUnreal1())
	{
		LogMessage("UnrealI.u: " + SHA1Sum::of_file(systemPath / "UnrealI.u"));
		LogMessage("UnrealShare.u: " + SHA1Sum::of_file(systemPath / "UnrealShare.u"));
	}
	else if (packages->IsKlingonHonorGuard())
	{
		LogMessage("Klingons.u: " + SHA1Sum::of_file(systemPath / "Klingons.u"));
	}
}

void Engine::GetLevelInfoObject()
{
	LevelInfo = UObject::Cast<ULevelInfo>(LevelPackage->GetUObject("LevelInfo", "LevelInfo0"));
	if (LaunchInfo.ue1Version < 300) // Unknown when this changed
	{
		for (int grr = 1; !LevelInfo && grr < 20; grr++)
			LevelInfo = UObject::Cast<ULevelInfo>(LevelPackage->GetUObject("LevelInfo", "LevelInfo" + std::to_string(grr)));
	}
	if (!LevelInfo)
		Exception::Throw("Could not find the LevelInfo object for " + LevelPackage->GetPackageName().ToString() + "!");
}

void Engine::GetLevelObject()
{
	Level = UObject::Cast<ULevel>(LevelPackage->GetUObject("Level", "MyLevel"));
	if (!Level)
		Exception::Throw("Could not find the Level object for" + LevelPackage->GetPackageName().ToString() + "!");

	Level->SetOwner(LevelInfo);

	if (LaunchInfo.IsDeusEx())
	{
		// Also try to find DeusExLevelInfo
		DeusExLevelInfo = UObject::Cast<UDeusExLevelInfo>(LevelPackage->GetUObject("DeusExLevelInfo", "DeusExLevelInfo0"));

		// Didn't find it. Keep searching.
		for (int grr = 1; !DeusExLevelInfo && grr < 20; grr++)
			DeusExLevelInfo = UObject::Cast<UDeusExLevelInfo>(LevelPackage->GetUObject("DeusExLevelInfo", "DeusExLevelInfo" + std::to_string(grr)));

		// Entry.dx does not have a DeusExLevelInfo
		/*
		if (!DeusExLevelInfo)
			Exception::Throw("Could not find the DeusExLevelInfo object for " + url.Map + "!");
		*/

		// Link giveObject for all events in the mission
		if (UConversationList* conList = GetDeusExMission())
		{
			for (UConItem* item = conList->conversations(); item; item = item->Next())
			{
				auto conversation = UObject::Cast<UConversation>(item->ConObject());
				for (UConEvent* e = conversation->eventList(); e; e = e->nextEvent())
				{
					EEventType eventType = (EEventType)e->eventType();
					if (eventType == EEventType::TransferObject)
					{
						if (auto transfer = UObject::Cast<UConEventTransferObject>(e))
						{
							UClass* cls = engine->packages->FindClass("DeusEx." + transfer->ObjectName());
							if (!cls)
								LogMessage("Could not find class for TransferObject: " + transfer->ObjectName());
							transfer->giveObject() = cls;
						}
					}
					else if (eventType == EEventType::CheckObject)
					{
						if (auto eventCheckObject = UObject::Cast<UConEventCheckObject>(e))
						{
							if (eventCheckObject->ObjectName().starts_with("NK_"))
							{
								eventCheckObject->checkObject() = nullptr;
							}
							else
							{
								UClass* cls = engine->packages->FindClass("DeusEx." + eventCheckObject->ObjectName());
								if (!cls)
									LogMessage("Could not find class for CheckObject: " + eventCheckObject->ObjectName());
								eventCheckObject->checkObject() = cls;
							}
						}
					}
				}

				// Remove comments from event lists:
				while (conversation->eventList() && (EEventType)conversation->eventList()->eventType() == EEventType::Comment)
					conversation->eventList() = conversation->eventList()->nextEvent();
				UConEvent* cur = conversation->eventList();
				while (cur != nullptr)
				{
					auto next = cur->nextEvent();
					if (next && (EEventType)next->eventType() == EEventType::Comment)
					{
						cur->nextEvent() = next->nextEvent();
					}
					else
					{
						cur = next;
					}
				}
			}
		}
	}
}

void Engine::LinkActorsToLevel()
{
	// Link actors to the level
	for (UActor* actor : Level->Actors)
	{
		if (actor)
		{
			actor->XLevel() = Level;
			actor->ChildActors.clear();
			Level->Collision.AddToCollision(actor);
		}
	}

	// BasedActors (who is standing on me, so I carry them when I move) is native, runtime-only
	// state - never serialized - while ActorBase() (what am I standing on) is a normal property
	// that does round-trip through a package/save. Without this, an actor that starts a level (or
	// a load) already resting on a mover has its own ActorBase() pointer intact, but the mover's
	// own BasedActors list is empty, so the mover has no way to carry it along once it moves.
	for (UActor* actor : Level->Actors)
	{
		if (actor)
		{
			actor->RelinkBasedActor();
			// Owner is serialized; the reverse runtime list is not. Reconstruct
			// it without replaying ownership events during load.
			if (actor->Owner()) actor->Owner()->ChildActors.push_back(actor);
		}
	}
}

const char* Engine::keynames[256] =
{
	/*00*/ "None", "LeftMouse", "RightMouse", "Cancel",
	/*04*/ "MiddleMouse", "Unknown05", "Unknown06", "Unknown07",
	/*08*/ "Backspace", "Tab", "Unknown0A", "Unknown0B",
	/*0C*/ "Unknown0C", "Enter", "Unknown0E", "Unknown0F",
	/*10*/ "Shift", "Ctrl", "Alt", "Pause",
	/*14*/ "CapsLock", "Unknown15", "Unknown16", "Unknown17",
	/*18*/ "Unknown18", "Unknown19", "Unknown1A", "Escape",
	/*1C*/ "Unknown1C", "Unknown1D", "Unknown1E", "Unknown1F",
	/*20*/ "Space", "PageUp", "PageDown", "End",
	/*24*/ "Home", "Left", "Up", "Right",
	/*28*/ "Down", "Select", "Print", "Execute",
	/*2C*/ "PrintScrn", "Insert", "Delete", "Help",
	/*30*/ "0", "1", "2", "3",
	/*34*/ "4", "5", "6", "7",
	/*38*/ "8", "9", "Unknown3A", "Unknown3B",
	/*3C*/ "Unknown3C", "Unknown3D", "Unknown3E", "Unknown3F",
	/*40*/ "Unknown40", "A", "B", "C",
	/*44*/ "D", "E", "F", "G",
	/*48*/ "H", "I", "J", "K",
	/*4C*/ "L", "M", "N", "O",
	/*50*/ "P", "Q", "R", "S",
	/*54*/ "T", "U", "V", "W",
	/*58*/ "X", "Y", "Z", "Unknown5B",
	/*5C*/ "Unknown5C", "Unknown5D", "Unknown5E", "Unknown5F",
	/*60*/ "NumPad0", "NumPad1", "NumPad2", "NumPad3",
	/*64*/ "NumPad4", "NumPad5", "NumPad6", "NumPad7",
	/*68*/ "NumPad8", "NumPad9", "GreyStar", "GreyPlus",
	/*6C*/ "Separator", "GreyMinus", "NumPadPeriod", "GreySlash",
	/*70*/ "F1", "F2", "F3", "F4",
	/*74*/ "F5", "F6", "F7", "F8",
	/*78*/ "F9", "F10", "F11", "F12",
	/*7C*/ "F13", "F14", "F15", "F16",
	/*80*/ "F17", "F18", "F19", "F20",
	/*84*/ "F21", "F22", "F23", "F24",
	/*88*/ "Unknown88", "Unknown89", "Unknown8A", "Unknown8B",
	/*8C*/ "Unknown8C", "Unknown8D", "Unknown8E", "Unknown8F",
	/*90*/ "NumLock", "ScrollLock", "Unknown92", "Unknown93",
	/*94*/ "Unknown94", "Unknown95", "Unknown96", "Unknown97",
	/*98*/ "Unknown98", "Unknown99", "Unknown9A", "Unknown9B",
	/*9C*/ "Unknown9C", "Unknown9D", "Unknown9E", "Unknown9F",
	/*A0*/ "LShift", "RShift", "LControl", "RControl",
	/*A4*/ "UnknownA4", "UnknownA5", "UnknownA6", "UnknownA7",
	/*A8*/ "UnknownA8", "UnknownA9", "UnknownAA", "UnknownAB",
	/*AC*/ "UnknownAC", "UnknownAD", "UnknownAE", "UnknownAF",
	/*B0*/ "UnknownB0", "UnknownB1", "UnknownB2", "UnknownB3",
	/*B4*/ "UnknownB4", "UnknownB5", "UnknownB6", "UnknownB7",
	/*B8*/ "UnknownB8", "UnknownB9", "Semicolon", "Equals",
	/*BC*/ "Comma", "Minus", "Period", "Slash",
	/*C0*/ "Tilde", "UnknownC1", "UnknownC2", "UnknownC3",
	/*C4*/ "UnknownC4", "UnknownC5", "UnknownC6", "UnknownC7",
	/*C8*/ "Joy1", "Joy2", "Joy3", "Joy4",
	/*CC*/ "Joy5", "Joy6", "Joy7", "Joy8",
	/*D0*/ "Joy9", "Joy10", "Joy11", "Joy12",
	/*D4*/ "Joy13", "Joy14", "Joy15", "Joy16",
	/*D8*/ "UnknownD8", "UnknownD9", "UnknownDA", "LeftBracket",
	/*DC*/ "Backslash", "RightBracket", "SingleQuote", "UnknownDF",
	/*E0*/ "JoyX", "JoyY", "JoyZ", "JoyR",
	/*E4*/ "MouseX", "MouseY", "MouseZ", "MouseW",
	/*E8*/ "JoyU", "JoyV", "UnknownEA", "UnknownEB",
	/*EC*/ "MouseWheelUp", "MouseWheelDown", "Unknown10E", "Unknown10F",
	/*F0*/ "JoyPovUp", "JoyPovDown", "JoyPovLeft", "JoyPovRight",
	/*F4*/ "UnknownF4", "UnknownF5", "Attn", "CrSel",
	/*F8*/ "ExSel", "ErEof", "Play", "Zoom",
	/*FC*/ "NoName", "PA1", "OEMClear", ""
};
