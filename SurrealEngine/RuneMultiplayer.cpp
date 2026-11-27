#include "RuneBattleLaws.h"
#include "RuneDesktopMenu.h"
#include "Precomp.h"
#include "RuneUpdates.h"
#include "RunePhysics.h"
#include "RuneSnow.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#ifndef __ANDROID__
#include "Utils/File.h"
#include <surrealwidgets/widgets/dialog/textinputdialog.h>
#endif
#include "RuneMultiplayer.h"
#include "RuneNetworkCamera.h"
#include "RuneNetwork.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "RuneController.h"
#include "RuneVisualEffects.h"
#include "RuneView.h"
#include "RuneMenu.h"
#include "RuneCharacters.h"
#include "RuneBot.h"
#include "RuneHud.h"
#include "RuneBloodlust.h"
#include "RuneDualWield.h"
#include "RuneCombat.h"
#include "RuneFeedback.h"
#include "Packages/Engine/Resources/USound.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "RenderDevice/VisualComparison.h"
#ifdef __ANDROID__
#include <surrealgpu/androidframepacingcontrol.h>
#endif
#include "Engine.h"
#include "GameWindow.h"
#include "Render/RenderSubsystem.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/UCanvas.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/USurrealClient.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "VM/ScriptCall.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <iostream>
#include "openrune/net/UdpSocket.h"
#include "openrune/net/Diagnostics.h"
#include "openrune/net/Presentation.h"
#include <iomanip>
#ifdef _WIN32
#include <sddl.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

using namespace openrune::net;
namespace
{
void require(bool value,const std::string& text)
{if(!value)throw std::runtime_error("Open Rune multiplayer: "+text);}
// Keep the network pump responsive without changing global timer resolution.
// A normal Win32 sleep can round a 1-2 ms poll up to a 15.6 ms scheduler tick.
void networkPause(unsigned milliseconds)
{
#ifdef _WIN32
    struct Timer {
        HANDLE handle=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
        ~Timer(){if(handle)CloseHandle(handle);}
    };
    thread_local Timer timer;
    if(timer.handle) {
        LARGE_INTEGER due;due.QuadPart=-static_cast<LONGLONG>(milliseconds)*10000;
        if(SetWaitableTimer(timer.handle,&due,0,nullptr,nullptr,FALSE) &&
            WaitForSingleObject(timer.handle,INFINITE)==WAIT_OBJECT_0)return;
    }
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
uint64_t nowMs()
{return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct MeasuredTick {
    TimingWindow* window;
    std::chrono::steady_clock::time_point start=window?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    ~MeasuredTick(){if(window)window->add(nowMs(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
};
struct ClientDiagnostics {
    TimingWindow frames,gaps;
    uint64_t lastWorld=0,nextReport=0,previousAt=0;
    Stats previous;
    std::vector<std::string> lines;
    std::string position;
    void reset(){*this=ClientDiagnostics{};}
    void world(uint64_t now){if(lastWorld && now>=lastWorld)gaps.add(now,double(now-lastWorld));lastWorld=now;}
    void report(uint64_t now,const Stats* stats,const PresentationTimeline::Stats* smoothing)
    {
        if(now<nextReport)return;nextReport=now+1000;
        auto frame=frames.summary(now),gap=gaps.summary(now);
        auto number=[](double n){std::ostringstream out;out<<std::fixed<<std::setprecision(1)<<n;return out.str();};
        lines={frame.count?"Frame ms: "+number(frame.mean)+" mean / "+number(frame.p95)+" p95":"Frame ms: not sampled",
            stats && stats->rttSamples?"RTT: "+std::to_string(stats->observedRttMs)+" ms / variation "+number(stats->rttVariationMs)+" ms":"RTT: waiting for acknowledged data"};
        const bool fresh=lastWorld && now>=lastWorld && now-lastWorld<=TimingWindow::HorizonMs;
        lines.push_back("World updates: "+(fresh && gap.mean>0?number(1000/gap.mean)+"/s":"—")+
            " / max gap "+number(gap.maximum)+" ms / age "+(lastWorld?std::to_string(now-lastWorld):"—")+" ms");
        if(stats && previousAt && now>previousAt && stats->sentBytes>=previous.sentBytes && stats->receivedBytes>=previous.receivedBytes && stats->dataSent>=previous.dataSent && stats->losses>=previous.losses) {
            auto seconds=double(now-previousAt)/1000;
            auto sent=stats->dataSent-previous.dataSent,timeouts=stats->losses-previous.losses;
            lines.push_back("Transport KiB/s: "+number(double(stats->sentBytes-previous.sentBytes)/1024/seconds)+" up / "+number(double(stats->receivedBytes-previous.receivedBytes)/1024/seconds)+" down");
            lines.push_back("TX timeouts: "+std::to_string(timeouts)+" / data sent "+std::to_string(sent)+" / duplicates "+std::to_string(stats->duplicates-previous.duplicates));
            lines.push_back("In flight: "+std::to_string(stats->inFlight)+" / window "+std::to_string(stats->congestionWindow)+" bytes");
        }
        if(stats){previous=*stats;previousAt=now;}else previousAt=0;
        lines.push_back(smoothing?"Smoothing: Adaptive / target "+number(smoothing->targetDelayMs)+" ms / buffered "+number(smoothing->bufferedMs)+" ms":"Smoothing: Original");
        if(!position.empty())lines.push_back(position);
        std::string log="OPEN_RUNE_CLIENT_DIAGNOSTICS";for(auto& line:lines)log+=" | "+line;LogMessage(log);
    }
};
std::string hex(std::span<const uint8_t> bytes)
{const char* digits="0123456789abcdef";std::string out;for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;}
const char* gameClass(GameMode mode) { return mode==GameMode::HeadBall?"HeadBall.HeadBallGame":mode==GameMode::Arena?"Arena.ArenaGameInfo":mode==GameMode::TeamDeathmatch?"RuneI.TeamGame":"RuneI.RuneMultiPlayer"; }
const char* teamName(uint8_t t) { static const char* names[]={"Red","Blue","Green","Gold"};return t<4?names[t]:"None"; }
const char* modeName(GameMode mode) { return mode==GameMode::HeadBall?"HeadBall":mode==GameMode::Arena?"Arena":mode==GameMode::TeamDeathmatch?"Team Deathmatch":"Deathmatch"; }
struct Invite
{
    std::string address="127.0.0.1",map="DM-Hildir",identity;
    uint16_t port=0,players=2;
    GameMode mode=GameMode::Deathmatch;
    uint8_t teams=2,arenaTeamSize=1,limbsScore=0;
    uint32_t laws=RuneBattleLaws::All;
    EntityRef owned;
    InvitationKey key{};
};
using Socket=UdpSocket;
bool sameAddress(const sockaddr_in& a,const sockaddr_in& b)
{return a.sin_addr.s_addr==b.sin_addr.s_addr && a.sin_port==b.sin_port;}
Bytes addressId(const sockaddr_in& a)
{
    Bytes out(6);std::memcpy(out.data(),&a.sin_addr.s_addr,4);std::memcpy(out.data()+4,&a.sin_port,2);return out;
}
void createInvitationDirectory(const std::filesystem::path& directory)
{
#ifdef _WIN32
    // Invitations are bearer credentials: exclude inherited broad read access.
    HANDLE token=nullptr;
    require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)!=0,"cannot inspect invitation owner");
    DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);
    std::vector<uint8_t> buffer(size);
    BOOL queried=GetTokenInformation(token,TokenUser,buffer.data(),size,&size);CloseHandle(token);
    require(queried!=0,"cannot read invitation owner");
    LPWSTR owner=nullptr;
    require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&owner)!=0,"cannot encode invitation owner");
    std::wstring acl=L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;"+std::wstring(owner)+L")";LocalFree(owner);
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)!=0,"cannot protect invitations");
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),descriptor,FALSE};
    BOOL created=CreateDirectoryW(directory.c_str(),&attributes);LocalFree(descriptor);
    require(created!=0,"choose a new private invitation directory");
#else
    require(std::filesystem::create_directory(directory),"choose a new invitation directory");
    std::filesystem::permissions(directory,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);
#endif
}
void writeInvite(const std::filesystem::path& path,const Invite& invite)
{
    std::ostringstream out;out<<"OPEN-RUNE-INVITE-5\n"<<invite.address<<' '<<invite.port<<'\n'
        <<invite.map<<'\n'<<invite.owned.id<<' '<<invite.owned.generation<<'\n'<<invite.identity<<'\n'<<hex(invite.key)<<'\n'<<invite.players<<'\n'<<int(invite.mode)<<'\n'<<int(invite.teams)<<' '<<int(invite.arenaTeamSize)<<' '<<int(invite.limbsScore)<<'\n'<<invite.laws<<'\n';
    auto text=out.str();
#ifdef _WIN32
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(file!=INVALID_HANDLE_VALUE,"cannot create private invitation file");
    DWORD count=0;BOOL written=WriteFile(file,text.data(),DWORD(text.size()),&count,nullptr);
    BOOL closed=CloseHandle(file);
    require(written && count==text.size() && closed,"cannot write invitation");
#else
    int fd=open(path.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);require(fd>=0,"cannot create private invitation file");
    auto count=write(fd,text.data(),text.size());int result=close(fd);
    require(count==ssize_t(text.size()) && !result,"cannot write invitation");
#endif
}
Invite readInvite(const std::string& path)
{
    require(std::filesystem::file_size(path)<=1024,"invitation is too large");
    std::ifstream input(path);std::string magic,key,trailing;Invite invite;unsigned port=0;
    require(bool(std::getline(input,magic)) && (magic=="OPEN-RUNE-INVITE-1" || magic=="OPEN-RUNE-INVITE-2" || (magic=="OPEN-RUNE-INVITE-3" || (magic=="OPEN-RUNE-INVITE-4" || magic=="OPEN-RUNE-INVITE-5"))),"unsupported invitation");
    require(bool(input>>invite.address>>port>>invite.map>>invite.owned.id>>invite.owned.generation>>invite.identity>>key) &&
        port>0 && port<=65535 && (invite.map.starts_with("DM-") || invite.map.starts_with("AR-") || invite.map.starts_with("HB-")) && invite.map.size()<=48 &&
        std::all_of(invite.map.begin(),invite.map.end(),[](unsigned char c){return std::isalnum(c) || c=='-' || c=='_';}) && invite.owned.id &&
        invite.owned.generation && invite.identity.size()==32 && key.size()==64,"invalid invitation fields");
    if(magic=="OPEN-RUNE-INVITE-2" || (magic=="OPEN-RUNE-INVITE-3" || (magic=="OPEN-RUNE-INVITE-4" || magic=="OPEN-RUNE-INVITE-5")))require(bool(input>>invite.players),"missing player count");
    if((magic=="OPEN-RUNE-INVITE-3" || (magic=="OPEN-RUNE-INVITE-4" || magic=="OPEN-RUNE-INVITE-5"))){unsigned mode=0;require(bool(input>>mode) && mode>=1 && mode<=4,"invalid game mode");invite.mode=GameMode(mode);}
    if((magic=="OPEN-RUNE-INVITE-4" || magic=="OPEN-RUNE-INVITE-5")) {
        unsigned teams=0,arena=0,limbs=0;
        require(bool(input>>teams>>arena>>limbs) && teams>=2 && teams<=4 && arena>=1 && arena<=4 && limbs<=1,"invalid invitation variants");
        invite.teams=uint8_t(teams);invite.arenaTeamSize=uint8_t(arena);invite.limbsScore=uint8_t(limbs);
    }
    if(magic=="OPEN-RUNE-INVITE-5")require(bool(input>>invite.laws) && !(invite.laws&~RuneBattleLaws::All),"invalid Laws of Battle");
    require(!(input>>trailing) && invite.players>=2 && invite.players<=16 && invite.owned.id<=invite.players,"invalid invitation roster");
    invite.port=uint16_t(port);
    auto digit=[](char c)->int {return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:-1;};
    for(size_t i=0;i<32;++i)
    {int a=digit(key[2*i]),b=digit(key[2*i+1]);require(a>=0 && b>=0,"invalid invitation key");invite.key[i]=uint8_t(a*16+b);}
    return invite;
}
struct PeerSlot
{
    openrune::CharacterChoice character;
    Invite invite;
    Socket socket;
    sockaddr_in address{};
    std::unique_ptr<SecureSession> session;
    std::unique_ptr<ReplicationPeer> peer;
    UPlayerPawn* pawn=nullptr;
    std::string displayName;bool bot=false;uint64_t nameAt=0;
    uint64_t attemptAt=0;
    bool joined=false, synchronizedMap=false;
    uint64_t actorCursor=0,combatCursor=0;
    std::deque<ActorEvent> bootstrap;
    PlayerScore savedScore;
    std::optional<MatchStatus> publishedStatus;
    explicit PeerSlot(const std::string& bindAddress):socket(bindAddress,0)
    {invite.address=bindAddress;invite.port=socket.port();invite.key=generateInvitationKey();auto id=generateInvitationKey();invite.identity=hex(std::span(id).first(16));}
};
void captureFrame(Engine& game,const std::string& path)
{
    if(path.empty())return;
    auto* device=game.window->GetRenderDevice();int w=device->GetRenderWidth(),h=device->GetRenderHeight();
    std::vector<TextureColor> pixels(size_t(w)*h);device->ReadPixels(pixels.data());
    std::ofstream file(path,std::ios::binary);require(bool(file),"cannot save frame");
    file<<"P6\n"<<w<<' '<<h<<"\n255\n";
    // One buffered write avoids millions of ostream calls per diagnostic
    // frame, which otherwise manufacture stalls during movement captures.
    std::vector<char> rgb(pixels.size()*3);size_t at=0;
    for(auto p:pixels){rgb[at++]=char(p.R);rgb[at++]=char(p.G);rgb[at++]=char(p.B);}
    file.write(rgb.data(),std::streamsize(rgb.size()));
}
// Only used by --rune-net-test-arena, with isolated saves. Normal matches use
// Rune's authored player starts, equipment and collision.
void prepareTestArena(Engine& game,UPlayerPawn* first,UPlayerPawn* second,bool grounded=false)
{
    auto start=first->Location();bool placed=false;
    for(int i=0;i<16 && !placed;++i)
    {
        float rad=float(i)*3.14159265f/8;vec3 direction(std::cos(rad),std::sin(rad),0);
        auto nearValue=start+direction*75.0f,farValue=start+direction*220.0f;
        if(grounded) {
            vec3 floor,normal,originFloor,originNormal;
            if(!first->Trace(floor,normal,nearValue-vec3(0,0,90),nearValue,false,vec3(16,16,0)) || normal.z<.7f ||
               !first->Trace(originFloor,originNormal,start-vec3(0,0,90),start,false,vec3(16,16,0)) ||
               std::abs(floor.z-originFloor.z)>8)continue;
        }
        if(!first->CheckLocation(farValue,22,42,true).first || !first->FastTrace(farValue,start) || !second->SetLocation(nearValue))continue;
        auto rotation=Rotator::FromVector(direction);first->SetRotation(rotation);first->ViewRotation()=rotation;
        second->SetRotation(Rotator(0,rotation.Yaw+32768,0));second->ViewRotation()=second->Rotation();placed=true;
    }
    require(placed,"test arena has no clear lane");
    if(first->GetUObject("Shield"))return;
    auto* shield=first->Spawn(game.packages->GetPackage("RuneI")->GetClass("VikingShield"),{},{},first->Location()+vec3(0,0,100),Rotator(0,0,0));
    require(shield!=nullptr,"test shield spawn");
    auto* copy=CallEvent(shield,"SpawnCopy",{ExpressionValue::ObjectValue(first)}).ToObject();require(copy!=nullptr,"test shield copy");
    CallEvent(first,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Idle","Begin");
}
}

void RunRuneMultiplayerServer(Engine& game,const std::string& directory)
{
    require(commandline->HasArg("","--save-directory"),"use a separate save directory for multiplayer");
    createInvitationDirectory(directory);
    const auto requestedMode=commandline->GetArg("","--rune-net-mode","dm");
    require(requestedMode=="dm" || requestedMode=="tdm" || requestedMode=="arena" || requestedMode=="headball","unsupported multiplayer mode");
    const auto mode=requestedMode=="headball"?GameMode::HeadBall:requestedMode=="arena"?GameMode::Arena:requestedMode=="tdm"?GameMode::TeamDeathmatch:GameMode::Deathmatch;
    const int playerCount=commandline->GetArgInt("","--rune-net-players",2);
    const int teamCount=commandline->GetArgInt("","--rune-net-teams",2);
    const int arenaTeamSize=commandline->GetArgInt("","--rune-net-arena-team-size",1);
    const bool limbsScore=commandline->HasArg("","--rune-net-limbs-score");
    require(teamCount>=2 && teamCount<=4 && arenaTeamSize>=1 && arenaTeamSize<=4 &&
        (!teamMode(mode) || playerCount>=teamCount) && (mode!=GameMode::Arena || playerCount>=arenaTeamSize*2) &&
        (mode==GameMode::HeadBall || !limbsScore),"invalid mode variant or insufficient players");
    std::vector<int> activeTeams;
    const int fragLimit=mode==GameMode::Arena?commandline->GetArgInt("","--rune-net-round-limit",commandline->GetArgInt("","--rune-net-frag-limit",20)):
        commandline->GetArgInt("","--rune-net-frag-limit",20);
    const int timeLimit=commandline->GetArgInt("","--rune-net-time-limit",600);
    const int intermission=commandline->GetArgInt("","--rune-net-intermission",8);
    require(playerCount>=2 && playerCount<=16 && fragLimit>=0 && fragLimit<=10000 &&
        timeLimit>=0 && timeLimit<=86400 && intermission>=2 && intermission<=60,"invalid multiplayer limits (2..16 players)");
    std::vector<std::string> maps;std::istringstream mapInput(commandline->GetArg("","--rune-net-maps",mode==GameMode::HeadBall?"HB-Agora":mode==GameMode::Arena?"AR-Champions":teamMode(mode)?"DM-Team_ships":"DM-Hildir,DM-Thorstadt"));
    for(std::string map;std::getline(mapInput,map,',');) {
        require(map.starts_with(mode==GameMode::HeadBall?"HB-":mode==GameMode::Arena?"AR-":"DM-") && map.size()<=48 &&
            std::all_of(map.begin(),map.end(),[](unsigned char c){return std::isalnum(c) || c=='-' || c=='_';}),"invalid map for selected mode");
        maps.push_back(map);
    }
    require(!maps.empty() && maps.size()<=32,"invalid map rotation");
    int selectedLaws=commandline->GetArgInt("","--rune-battle-laws",RuneBattleLaws::All);
    require(selectedLaws>=0 && !(uint32_t(selectedLaws)&~RuneBattleLaws::All),"invalid Laws of Battle mask");
    if(commandline->GetArg("","--rune-normal-bloodlust-moves","1")=="0")selectedLaws&=~RuneBattleLaws::Normal;
    for(unsigned i=0;i<RuneBattleLaws::Keys.size();++i){
        std::string option=std::string("--rune-law-")+RuneBattleLaws::Keys[i];
        if(commandline->HasArg("",option)){
            auto value=commandline->GetArg("",option,"1");
            require(value=="0"||value=="1","Laws of Battle options must be 0 or 1");
            if(value=="1")selectedLaws|=1u<<i;else selectedLaws&=~(1u<<i);
        }
    }
    RuneBattleLaws::MatchScope lawsScope{uint32_t(selectedLaws)};
    game.NetworkAuthority=true;RuneBloodlust::AllowNormal=commandline->GetArg("","--rune-normal-bloodlust-moves","1")!="0";
    auto bindAddress=commandline->GetArg("","--rune-net-address","127.0.0.1");
    std::vector<std::unique_ptr<PeerSlot>> slots;
    for(int i=0;i<playerCount;++i)slots.push_back(std::make_unique<PeerSlot>(bindAddress));
    const bool testMatch=commandline->HasArg("","--rune-net-test-match");
    const bool testTeam=commandline->HasArg("","--rune-net-test-team");
    require(!testTeam || (teamMode(mode) && playerCount==4),"team fixture needs four team players");
    const bool testHeadBall=commandline->HasArg("","--rune-net-test-headball");
    require(!testHeadBall || mode==GameMode::HeadBall,"HeadBall fixture requires HeadBall mode");
    uint64_t headBallStartTick=0;UActor* testHead=nullptr;int headScoreExpected=0;bool headScored=false;
    const bool testArenaMode=commandline->HasArg("","--rune-net-test-arena-mode");
    require(!testArenaMode || mode==GameMode::Arena,"Arena fixture requires Arena mode");
    uint64_t arenaFightTick=0,arenaDeathTick=0;int arenaKilledRound=0;
    UBrush* arenaTestBrush=nullptr;vec3 arenaBrushStart{};
    vec3 allyPosition{};std::optional<vec3> teamAnchor;
    std::unique_ptr<RuneNetworkAuthority> authority;
    size_t mapIndex=0;World world;
    if(mode==GameMode::HeadBall)game.packages->GetPackage("HeadBall")->GetClass("HeadBallGame")->GetDefaultObject<UObject>()->SetBool("bLimbsScore",limbsScore);
    if(mode==GameMode::Arena)game.packages->GetPackage("Arena")->GetClass("ArenaGameInfo")->GetDefaultObject<UObject>()->SetInt("MaxTeamSupport",arenaTeamSize);
    auto retire=[&](size_t index) {
        auto& slot=slots[index];
        if(slot->pawn) {
            auto* p=slot->pawn;auto* info=p->GetUObject("PlayerReplicationInfo");
            if(info) {slot->savedScore.frags=int32_t(info->GetFloat("Score"));slot->savedScore.deaths=uint16_t(std::clamp(info->GetFloat("Deaths"),0.f,65535.f));}
            authority->unbind(index+1);CallEvent(game.GameInfo,"Logout",{ExpressionValue::ObjectValue(p)});
            // Destroy carried inventory, leaving already thrown world items intact.
            for(auto* a:game.Level->Actors)if(a && a->Owner()==p && a->IsA("Inventory"))a->Destroy();
            p->Destroy();slot->pawn=nullptr;
        }
        slot->joined=false;slot->peer.reset();slot->session.reset();slot->publishedStatus.reset();slot->bootstrap.clear();
    };
    const uint32_t botNameSeed=RuneSnow::NewSeed();
    uint32_t weatherSeed=0;uint64_t weatherStartMs=0;
    bool roundInitialized=false;
    auto loadRound=[&] {
        authority.reset();
        auto url=game.GetDefaultURL(maps[mapIndex]);
        // Retain original kill/suicide scoring, but this session owns the clock
        // and travel. Original server-travel callbacks cannot race the barrier.
        url.Options.push_back(std::string("game=")+gameClass(mode));url.Options.push_back("FragLimit=0");url.Options.push_back("TimeLimit=0");
        game.LoadMap(url);game.GameInfo->SetBool("bDontRestart",true);
        weatherSeed=RuneSnow::NewSeed();weatherStartMs=nowMs();
        if(commandline->HasArg("","--rune-net-weather-trace"))std::cout<<"OPEN_RUNE_HOST_WEATHER seed="<<weatherSeed<<" start="<<weatherStartMs<<std::endl;
        arenaFightTick=0;arenaKilledRound=0;arenaDeathTick=0;arenaTestBrush=nullptr;headBallStartTick=0;testHead=nullptr;headScored=false;
        if(mode==GameMode::Arena)require(game.GameInfo->GetInt("maxArenaTeam")==arenaTeamSize,"map cannot support requested Arena team size");
        activeTeams.clear();
        if(teamMode(mode)) {
            game.GameInfo->SetInt("MaxTeams",4);
            auto* supported=static_cast<int32_t*>(game.GameInfo->GetProperty("TeamSupported"));
            for(int team=0;team<4 && int(activeTeams.size())<teamCount;++team)if(supported[team])activeTeams.push_back(team);
            require(int(activeTeams.size())==teamCount,"map does not support requested team count");game.GameInfo->SetFloat("FriendlyFireScale",0);
            game.GameInfo->SetFloat("GoalTeamScore",0);
            // Fixed invitation slots already balance teams. HeadBall otherwise
            // rebalances again after Login has inserted the new player.
            if(mode==GameMode::HeadBall){game.GameInfo->SetBool("bEvenTeams",false);game.GameInfo->SetBool("bLimbsScore",limbsScore);}
        }
        authority=std::make_unique<RuneNetworkAuthority>(game);
        for(size_t i=0;i<slots.size();++i) {
            slots[i]->publishedStatus.reset();slots[i]->savedScore={};slots[i]->actorCursor=slots[i]->combatCursor=0;slots[i]->bootstrap.clear();
            slots[i]->pawn=game.LoginNetworkPlayer(slots[i]->displayName.empty()?"Warrior "+std::to_string(i+1):slots[i]->displayName,teamMode(mode)?activeTeams[i%activeTeams.size()]:-1,slots[i]->character.character,slots[i]->character.skin);
            if(teamMode(mode))require(slots[i]->pawn->GetUObject("PlayerReplicationInfo")->GetByte("Team")==activeTeams[i%activeTeams.size()],"team assignment failed");
            require(authority->bind(i+1,slots[i]->pawn,1),"cannot assign player");
            auto owned=authority->identity(slots[i]->pawn);
            require(owned.id==i+1 && owned.generation==1,"player identity changed across map load");
            slots[i]->invite.owned={uint32_t(i+1),1};
        }
        for(size_t i=0;i<slots.size();++i)if(!slots[i]->joined && roundInitialized)retire(i);
        if(testTeam && slots[0]->pawn && slots[2]->pawn) {
            if(teamAnchor) {
                require(slots[2]->pawn->SetLocation(*teamAnchor+vec3(0,0,400)),"park fixture ally");
                require(slots[0]->pawn->SetLocation(*teamAnchor),"restore fixture anchor");
            }
            prepareTestArena(game,slots[0]->pawn,slots[2]->pawn,true);teamAnchor=slots[0]->pawn->Location();
        }
        require(authority->capture(world),"initial world capture failed");
        std::array<int,4> starts{};int queueZones=0,goals=0,pumps=0,championStarts=0,challengerStarts=0,powerPickups=0;
        for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe()) {
            if(a->IsA("PlayerStart") && a->GetByte("TeamNumber")<4)++starts[a->GetByte("TeamNumber")];
            if(a->IsA("ArenaStart")){if(a->GetBool("bChampion") || a->GetBool("bChampionTeam"))++championStarts;else ++challengerStarts;}
            if(a->IsA("RuneOfPower")||a->IsA("RuneOfPowerRefill"))++powerPickups;
            if(a->IsA("QueueZone"))++queueZones;if(a->IsA("HeadBallZone"))++goals;if(a->IsA("HBGamePump"))++pumps;
        }
        std::cout<<"OPEN_RUNE_MAP_CHECK map="<<maps[mapIndex]<<" entities="<<world.size()<<" starts="<<starts[0]<<","<<starts[1]<<","<<starts[2]<<","<<starts[3]
            <<" rune_pickups="<<powerPickups<<" arena_capacity="<<std::min(championStarts,challengerStarts)<<" queues="<<queueZones<<" goals="<<goals<<" pumps="<<pumps<<" arena_size="<<(mode==GameMode::Arena?game.GameInfo->GetInt("maxArenaTeam"):0)<<std::endl;
    };
    // Validate authored variant support for the entire rotation before any
    // invitation is published. File hashes alone cannot establish team starts.
    if(maps.size()>1)for(const auto& name:maps) {
        auto url=game.GetDefaultURL(name);url.Options.push_back(std::string("game=")+gameClass(mode));
        url.Options.push_back("FragLimit=0");url.Options.push_back("TimeLimit=0");game.LoadMap(url);
        if(mode==GameMode::Arena)require(game.GameInfo->GetInt("maxArenaTeam")==arenaTeamSize,name+": unsupported Arena team size");
        if(teamMode(mode)) {
            auto* supported=static_cast<int32_t*>(game.GameInfo->GetProperty("TeamSupported"));
            require(std::count_if(supported,supported+4,[](int value){return value!=0;})>=teamCount,name+": unsupported team count");
        }
    }
    loadRound();
    // Check every map before publishing invitations, not midway through a match.
    std::vector<MatchContract> contracts;
    for(const auto& map:maps)contracts.push_back(MakeRuneMatchContract(game,map,uint16_t(playerCount),mode,uint8_t(teamCount),uint8_t(arenaTeamSize),limbsScore));
    auto match=contracts[0];
    for(size_t i=0;i<slots.size();++i) {
        slots[i]->invite.teams=uint8_t(teamCount);slots[i]->invite.arenaTeamSize=uint8_t(arenaTeamSize);slots[i]->invite.limbsScore=limbsScore;
        slots[i]->invite.laws=RuneBattleLaws::Multiplayer.load();
        slots[i]->invite.map=maps[0];slots[i]->invite.mode=mode;slots[i]->invite.players=uint16_t(playerCount);
        writeInvite(std::filesystem::path(directory)/("player"+std::to_string(i+1)+".rune-invite"),slots[i]->invite);
    }
    const bool sparring=commandline->HasArg("","--rune-net-sparring-arena");
    const bool testArena=commandline->HasArg("","--rune-net-test-arena");
    const auto nativeFixture=commandline->GetArg("","--rune-net-native-fixture","");
    require(nativeFixture.empty() || nativeFixture=="dual" || nativeFixture=="blood" || nativeFixture=="bash","invalid native fixture");
    auto prepareNativeFixture=[&] {
        if(nativeFixture.empty())return;
        prepareTestArena(game,slots[0]->pawn,slots[1]->pawn);
        prepareTestArena(game,slots[1]->pawn,slots[0]->pawn);
        for(auto& slot:slots) {
            auto* p=slot->pawn;p->Health()=1000;
            if(nativeFixture=="dual") {
                auto* item=p->Spawn(game.packages->GetPackage("RuneI")->GetClass("VikingShortSword"),{},{},p->Location()+vec3(0,0,100),{});
                require(item!=nullptr,"native fixture weapon");item->SetFloat("RespawnTime",0);
                auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(p)}).ToObject();
                require(copy!=nullptr,"native fixture copy");CallEvent(p,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});
            } else if(nativeFixture=="blood") {p->SetInt("Strength",p->GetInt("MaxStrength"));p->SetBool("bBloodLust",true);}
        }
    };
    if(commandline->HasArg("","--rune-net-stair-route")) {
        require(commandline->HasArg("","--save-directory"),"stair fixture requires isolated saves");
        auto route=commandline->GetArg("","--rune-net-stair-route","");std::replace(route.begin(),route.end(),',',' ');
        vec3 at;int facing;std::string extra;std::istringstream values(route);
        require(bool(values>>at.x>>at.y>>at.z>>facing) && !(values>>extra) &&
            std::isfinite(at.x) && std::isfinite(at.y) && std::isfinite(at.z),"invalid stair route");
        require(slots[0]->pawn->SetLocation(at),"stair fixture placement failed");
        slots[0]->pawn->Velocity()=vec3(0);slots[0]->pawn->ViewRotation()=Rotator(0,facing,0);
        slots[0]->pawn->Rotation()=Rotator(0,facing,0);
    }
    if(testArena || sparring)prepareTestArena(game,slots[0]->pawn,slots[1]->pawn);
    if(sparring)prepareTestArena(game,slots[1]->pawn,slots[0]->pawn);
    require(authority->capture(world),"prepared world capture failed");
    roundInitialized=true;
    std::cout<<"OPEN_RUNE_SERVER_READY players="<<playerCount<<" map="<<maps[0]<<" invitations="<<directory<<std::endl;
    auto started=nowMs();auto seconds=commandline->GetArgInt("","--rune-net-seconds",0);
    FixedSimulation simulation;bool running=false;uint64_t snapshots=0,actions=0,contacts=0;
    const bool diagnostics=commandline->HasArg("","--rune-net-diagnostics");
    TimingWindow tickTimings;uint64_t nextDiagnostics=started+2000;
    double stageInputMs=0,stageLevelMs=0,stagePhysicsMs=0,stageSnapshotMs=0;unsigned stageTicks=0,stageSnapshots=0;
    MatchStatus state;state.mode=mode;state.teams=teamMode(mode)?4:2;state.fragLimit=uint32_t(fragLimit);
    std::optional<MatchStatus> lastStatus;uint64_t endedAt=0,travelAt=0,roundStartedAt=0;uint32_t round=1;
    auto readScores=[&] {
        state.weatherSeed=weatherSeed;state.weatherStartMs=weatherStartMs;
        state.scores.clear();
        for(size_t i=0;i<slots.size();++i) {
            if(!slots[i]->pawn) {
                auto score=slots[i]->savedScore;score.player=uint32_t(i+1);score.pawn={};score.team=teamMode(mode)?uint8_t(activeTeams[i%activeTeams.size()]):255;
                state.scores.push_back(score);continue;
            }
            auto* info=slots[i]->pawn->GetUObject("PlayerReplicationInfo");require(info!=nullptr,"missing player score");
            state.scores.push_back({uint32_t(i+1),int32_t(std::clamp(info->GetFloat("Score"),-1000000.0f,1000000.0f)),
                uint16_t(std::clamp(info->GetFloat("Deaths"),0.0f,65535.0f)),
                mode!=GameMode::Deathmatch?info->GetByte("Team"):uint8_t(255),
                mode==GameMode::Arena?info->GetByte("TeamID"):uint8_t(255),slots[i]->joined?authority->identity(slots[i]->pawn):EntityRef{}});
        }
        if(mode==GameMode::Arena) {
            state.arenaPhase=game.GameInfo->GetByte("GameState");
            state.arenaCountdown=uint8_t(std::clamp(int32_t(game.GameInfo->GetInt("curTimer")),0,120));
            state.arenaRound=uint32_t(std::max(0,int32_t(game.GameInfo->GetInt("CurrentMatch"))));
        }
        if(mode==GameMode::HeadBall) {
            state.headBallStarted=game.GameInfo->GetBool("bGameStarted");
            state.headBallCountdown=state.headBallStarted?0:uint8_t(std::clamp(int32_t(game.GameInfo->GetInt("CountTimer")),0,120));
            auto* ready=static_cast<uint8_t*>(game.GameInfo->GetProperty("TeamReady"));
            state.readyTeams={ready[0],ready[1],ready[2],ready[3]};
        }
        if(teamMode(mode)) {
            auto** teams=static_cast<UObject**>(game.GameInfo->GetProperty("Teams"));
            for(size_t t=0;t<4;++t){require(teams[t]!=nullptr,"missing team");state.teamScores[t]=int32_t(teams[t]->GetFloat("Score"));}
        }
    };
    readScores();
    const bool discoverable=commandline->HasArg("","--rune-net-discovery");
    uint64_t lastDiscoveryStatus=0;
    RuneMultiplayer::HostReady.store(true);
    std::vector<UdpSocket*> waitSockets;waitSockets.reserve(slots.size());
    const bool idleWait=commandline->GetArgInt("","--rune-net-idle-wait",1)!=0; // 0: previous fixed 1 ms poll.
    while(!game.quit && !RuneMultiplayer::HostStop.load() && (!seconds || nowMs()-started<uint64_t(seconds)*1000))
    {
        auto now=nowMs();
        bool hostActivity=false;const auto ticksBefore=simulation.ticks();
        if(discoverable && (!lastDiscoveryStatus || now-lastDiscoveryStatus>=1000)) {
            lastDiscoveryStatus=now;
            const auto file=std::filesystem::path(directory)/"discovery-status.json";
            const auto updated=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::ofstream out(file.string()+".tmp");
            const auto count=std::count_if(slots.begin(),slots.end(),[](const auto& s){return s->joined;});
            out<<"{\"ready\":true,\"protocol\":"<<ProtocolVersion<<",\"updated\":"<<updated<<",\"map\":\""<<maps[mapIndex]
                <<"\",\"mode\":\""<<modeName(mode)<<"\",\"players\":"<<count<<",\"capacity\":"<<playerCount<<",\"occupied\":[";
            bool comma=false;for(const auto& s:slots)if(s->session){if(comma)out<<',';out<<s->invite.owned.id;comma=true;}out<<"]}";out.close();
#ifdef _WIN32
            // Unlike POSIX rename, std::filesystem::rename does not replace on Windows.
            MoveFileExW(std::filesystem::path(file.string()+".tmp").c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
#else
            std::error_code error;std::filesystem::rename(file.string()+".tmp",file,error);
#endif
        }
        for(auto& slot:slots)
        {
            slot->socket.receive([&](const sockaddr_in& source,std::span<const uint8_t> bytes)
            {
                hostActivity=true;
                if(!slot->session)
                {
                    if(bytes.size()<25 || bytes[0]!=22 || bytes[13]!=1)return; // Only a DTLS ClientHello may claim a vacant slot.
                    // Public slots receive fresh credentials from the app-owned admission service.
                    // Read only when vacant, and preserve the authority's roster/map contract.
                    if(discoverable) {
                        try {
                            auto fresh=readInvite((std::filesystem::path(directory)/("player"+std::to_string(slot->invite.owned.id)+".rune-invite")).string());
                            if(fresh.owned!=slot->invite.owned || fresh.port!=slot->invite.port || fresh.map!=slot->invite.map ||
                                fresh.players!=slot->invite.players || fresh.mode!=slot->invite.mode || fresh.teams!=slot->invite.teams ||
                                fresh.laws!=slot->invite.laws || fresh.arenaTeamSize!=slot->invite.arenaTeamSize || fresh.limbsScore!=slot->invite.limbsScore)return;
                            if(slot->invite.identity!=fresh.identity)slot->savedScore=PlayerScore{};
                            slot->invite.key=fresh.key;slot->invite.identity=fresh.identity;
                        }catch(...){return;}
                    }
                    slot->address=source;slot->attemptAt=now;
                    slot->session=std::make_unique<SecureSession>(true,slot->invite.key,slot->invite.identity,addressId(source));
                    slot->peer=std::make_unique<ReplicationPeer>(*slot->session,contracts[0]);
                    slot->synchronizedMap=mapIndex==0;slot->actorCursor=slot->combatCursor=0;slot->bootstrap.clear();
                }
                if(sameAddress(source,slot->address))slot->session->receiveDatagram(bytes);
            });
            if(!slot->session)continue;
            slot->session->step(now);slot->peer->step();
            if(slot->peer->playerProfile() && now>=slot->nameAt) {
                auto profile=*slot->peer->playerProfile();uint32_t index=uint32_t(&slot-&slots[0])+1;
                auto name=profile.bot?botName(index,botNameSeed):profile.name;
                if(name!=slot->displayName||profile.bot!=slot->bot){slot->displayName=name;slot->bot=profile.bot;slot->nameAt=now+1000;
                    if(slot->pawn)if(auto* info=slot->pawn->GetUObject("PlayerReplicationInfo"))info->SetString("PlayerName",name);
                    std::cout<<"OPEN_RUNE_PLAYER_NAME player="<<index<<" name="<<std::quoted(name)<<" bot="<<slot->bot<<std::endl;
                }
            }

            if(slot->peer->matchMismatch())
            {std::cout<<"OPEN_RUNE_JOIN_REJECTED content_mismatch"<<std::endl;retire(size_t(&slot-&slots[0]));continue;}
            if(slot->peer->remoteClosed())
            {std::cout<<"OPEN_RUNE_PLAYER_LEFT id="<<slot->invite.owned.id<<std::endl;retire(size_t(&slot-&slots[0]));continue;}
            if(slot->session->failed() || (!slot->joined && now-slot->attemptAt>15000))
            {
                std::cout<<"OPEN_RUNE_PLAYER_DISCONNECTED id="<<slot->invite.owned.id<<std::endl;retire(size_t(&slot-&slots[0]));continue;
            }
            if(slot->peer->matchAgreed() && !slot->synchronizedMap) {
                if(slot->peer->travel(match))slot->synchronizedMap=true;
                continue;
            }
            if(slot->peer->matchAgreed() && !slot->joined) {
                size_t index=size_t(&slot-&slots[0]);
                require(slot->peer->characterChoice().has_value(),"missing authenticated character choice");
                auto choice=*slot->peer->characterChoice();
                if(slot->pawn && choice!=slot->character) {
                    auto* old=slot->pawn;authority->unbind(index+1);
                    CallEvent(game.GameInfo,"Logout",{ExpressionValue::ObjectValue(old)});
                    for(auto* a:game.Level->Actors)if(a && a->Owner()==old && a->IsA("Inventory"))a->Destroy();
                    old->Destroy();slot->pawn=nullptr;
                }
                slot->character=choice;
                if(!slot->pawn) {
                    slot->pawn=game.LoginNetworkPlayer(slot->displayName,teamMode(mode)?activeTeams[index%activeTeams.size()]:-1,choice.character,choice.skin);
                    require(authority->bind(index+1,slot->pawn,1),"rejoin bind failed");
                    auto* info=slot->pawn->GetUObject("PlayerReplicationInfo");info->SetFloat("Score",float(slot->savedScore.frags));info->SetFloat("Deaths",float(slot->savedScore.deaths));
                }
                slot->joined=true;slot->publishedStatus.reset();
                require(authority->capture(world),"join world capture");
                auto baseline=authority->actorBaseline();slot->bootstrap.assign(baseline.begin(),baseline.end());
                slot->actorCursor=authority->actorWatermark();slot->combatCursor=authority->combatWatermark();
                // A newly selected class replaces a reserved pawn. Refresh every
                // bootstrap: another joining peer may still hold that old pawn
                // in its first snapshot, before simulation is allowed to start.
                // The final world of a round is published at tick + 2.
                // Keep a joining player's bootstrap at or beyond that tick
                // when refreshing peers during intermission.
                const auto snapshotTick=simulation.ticks()+(endedAt?2:1);
                for(auto& other:slots)if(other->joined)
                    require(other->peer->snapshots.capture(world,snapshotTick),"join snapshot enqueue");
                readScores();
                std::cout<<"OPEN_RUNE_PLAYER_JOINED id="<<slot->invite.owned.id<<" pawn="<<authority->identity(slot->pawn).id<<" character="<<openrune::Characters[slot->character.character].className<<" skin="<<int(slot->character.skin)<<" team="<<int(slot->pawn->GetUObject("PlayerReplicationInfo")->GetByte("Team"))<<" power="<<slot->pawn->GetInt("RunePower")<<" capacity="<<slot->pawn->GetInt("MaxPower")<<" radius="<<slot->pawn->CollisionRadius()<<" height="<<slot->pawn->CollisionHeight()<<std::endl;
            }
            if(slot->pawn)slot->pawn->ShieldTaunt.bot=slot->bot;
            while(!slot->bootstrap.empty()) {
                if(!slot->peer->publishActor(slot->bootstrap.front()))break;
                slot->bootstrap.pop_front();
            }
        }
        if(game.quit)break;
        if(endedAt && now>=endedAt+uint64_t(intermission)*1000) {
            const auto next=(mapIndex+1)%maps.size();bool sent=true;
            for(auto& slot:slots)if(slot->joined && slot->peer->matchAgreed() && !slot->peer->travel(contracts[next]))sent=false;
            if(sent) {
                mapIndex=next;++round;match=contracts[next];loadRound();simulation=FixedSimulation{};
                running=false;endedAt=0;travelAt=now;state.phase=MatchPhase::Waiting;state.winner=0;state.winningTeam=255;lastStatus.reset();readScores();
                for(auto& slot:slots)if(slot->joined)require(slot->peer->snapshots.capture(world,1),"travel initial snapshot failed");
                std::cout<<"OPEN_RUNE_MAP_TRAVEL round="<<round<<" map="<<maps[mapIndex]<<" connections="<<slots.size()<<std::endl;
            }
        }
        if(travelAt && now-travelAt>=45000) {
            // A slow or departed peer cannot terminate a persistent host. Keep
            // loaded players and release only slots that failed the map barrier.
            for(size_t i=0;i<slots.size();++i)if(slots[i]->joined &&
                (!slots[i]->bootstrap.empty() || !slots[i]->peer->matchAgreed() || !slots[i]->peer->snapshots.baselineId())) {
                std::cout<<"OPEN_RUNE_PLAYER_MAP_TIMEOUT id="<<i+1<<std::endl;retire(i);
            }
            travelAt=0;
        }
        // Each peer drains reliable actor events at its own pace. A full queue on
        // one slow link (a relayed public player) must not withhold spawns from
        // the others: their snapshots would wait on actors they never receive.
        for(auto& slot:slots)if(slot->joined && slot->bootstrap.empty())
            for(const auto& event:authority->pendingActors()) {
                if(slot->actorCursor>=event.id)continue;
                auto local=event;local.id=slot->peer->lastActorPublished()+1;
                if(!slot->peer->publishActor(local))break;
                slot->actorCursor=event.id;
            }
        {
            // Retain events until every joined peer has queued them.
            uint64_t floor=UINT64_MAX,released=0;
            for(const auto& slot:slots)if(slot->joined)floor=std::min(floor,slot->actorCursor);
            for(const auto& event:authority->pendingActors())if(event.id<=floor)released=event.id;else break;
            if(released)require(authority->releaseActorsThrough(released),"actor retention failed");
        }
        // Drop only a peer that has fallen far behind, before the shared retention
        // limits (1024 actor events, 64 combat results) would fault the match.
        for(size_t i=0;i<slots.size();++i)if(slots[i]->joined && authority) {
            const auto actorLag=authority->actorWatermark()-std::min(authority->actorWatermark(),slots[i]->actorCursor);
            const auto combatLag=authority->combatWatermark()-std::min(authority->combatWatermark(),slots[i]->combatCursor);
            if(actorLag>768 || combatLag>48) {
                std::cout<<"OPEN_RUNE_PLAYER_LAG_DROP id="<<i+1<<" actor_lag="<<actorLag<<" combat_lag="<<combatLag<<std::endl;
                retire(i);
            }
        }
        if(!running && std::count_if(slots.begin(),slots.end(),[](const auto& s){return s->joined;})>=(testMatch || testHeadBall || testArenaMode || testTeam || testArena || commandline->HasArg("","--rune-net-tag-fixture") || commandline->HasArg("","--rune-net-character-fixture")?playerCount:mode==GameMode::Arena?arenaTeamSize*2:teamMode(mode)?teamCount:2) &&
            std::all_of(slots.begin(),slots.end(),[](const auto& s){return !s->joined || (s->bootstrap.empty() && s->peer->matchAgreed() && s->peer->snapshots.baselineId()>0);}))
        {for(size_t i=0;i<slots.size();++i)if(!slots[i]->joined && !slots[i]->session && slots[i]->pawn)retire(i);
            prepareNativeFixture();
            running=true;travelAt=0;roundStartedAt=now;state.phase=MatchPhase::Playing;std::cout<<"OPEN_RUNE_MATCH_STARTED round="<<round<<" map="<<maps[mapIndex]<<std::endl;}
        if(running && !endedAt)simulation.advance(now*1000,[&](uint64_t tick,float dt)
        {
            MeasuredTick measured{diagnostics?&tickTimings:nullptr};
            auto stageStart=diagnostics?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
            if(endedAt)return;
            authority->beginTick(tick,now);
            if(commandline->HasArg("","--rune-net-tag-fixture") && tick==1) {
                require(playerCount>=3 && slots[0]->pawn && slots[2]->pawn,"tag fixture needs three players");
                prepareTestArena(game,slots[0]->pawn,slots[2]->pawn,true);
                if(commandline->HasArg("","--rune-net-name-fixture")){
                    auto forward=vec3(std::cos(slots[0]->pawn->Rotation().YawRadians()),std::sin(slots[0]->pawn->Rotation().YawRadians()),0),side=cross(vec3(0,0,1),forward);
                    for(size_t i=1;i<slots.size();i+=2)for(float offset:{70.f,-70.f,100.f,-100.f})if(slots[i]->pawn->SetLocation(slots[2]->pawn->Location()+side*(offset+float(i-1)*15.f))){slots[i]->pawn->SetRotation(slots[2]->pawn->Rotation());slots[i]->pawn->ViewRotation()=slots[i]->pawn->Rotation();break;}
                }
            }
            for(size_t i=0;i<slots.size();++i)
            {
                auto& slot=slots[i];if(!slot->joined || !slot->peer->matchAgreed())continue;slot->peer->commands.setLife(authority->playerLife(i+1));auto held=slot->peer->commands.held(now);
                if(!held.sequence)held.yaw=uint16_t(slot->pawn->ViewRotation().Yaw);
                if(slot->pawn->Health()>0)require(authority->input(i+1,held),"owned movement rejected");
                for(auto action:slot->peer->commands.drainActions(now))
                {
                    bool accepted=authority->action(i+1,action,now);++actions;
                    require(slot->peer->decideAction(action.action.id,accepted?Admission::Started:Admission::ExecutionRejected),"action decision overflow");
                    slot->peer->commands.setLife(authority->playerLife(i+1));
                    if(accepted && action.action.kind==ActionKind::Respawn) {
                        require(slot->pawn->Class==game.packages->FindClass(openrune::Characters[slot->character.character].className) && slot->pawn->GetInt("CurrentSkin")==slot->character.skin,"respawn changed character or skin");
                        std::cout<<"OPEN_RUNE_CHARACTER_RESPAWN player="<<i+1<<" character="<<int(slot->character.character)<<" skin="<<int(slot->character.skin)<<" team="<<int(slot->pawn->GetUObject("PlayerReplicationInfo")->GetByte("Team"))<<" power="<<slot->pawn->GetInt("RunePower")<<" capacity="<<slot->pawn->GetInt("MaxPower")<<" radius="<<slot->pawn->CollisionRadius()<<" height="<<slot->pawn->CollisionHeight()<<std::endl;
                    }
                    if(teamMode(mode))require(slot->pawn->GetUObject("PlayerReplicationInfo")->GetByte("Team")==activeTeams[i%activeTeams.size()],"respawn changed team");
                    if(sparring && accepted && action.action.kind==ActionKind::Respawn)
                    {
                        prepareTestArena(game,slots[i]->pawn,slots[(i+1)%slots.size()]->pawn);
                        std::cout<<"OPEN_RUNE_SPARRING_RESET player="<<i+1<<std::endl;
                    }
                    std::cout<<"OPEN_RUNE_ACTION player="<<i+1<<" kind="<<int(action.action.kind)<<" accepted="<<accepted<<std::endl;
                }
            }
            authority->commitContacts();
            const bool fixtureRosterComplete=std::all_of(slots.begin(),slots.end(),[](const auto& slot){return slot->joined && slot->pawn;});
            if(testHeadBall && fixtureRosterComplete) {
                if(!headBallStartTick && tick>=60 && tick%60==0) {
                    auto* ready=static_cast<uint8_t*>(game.GameInfo->GetProperty("TeamReady"));
                    auto next=std::find_if(activeTeams.begin(),activeTeams.end(),[&](int team){return !ready[team];});
                    if(next!=activeTeams.end()) {
                    size_t slotIndex=size_t(next-activeTeams.begin());size_t team=*next;auto* pawn=slots[slotIndex]->pawn;UActor* pump=nullptr;
                    for(auto* actor:game.Level->Actors)if(actor && actor->IsA("HBGamePump") && actor->GetByte("TeamNumber")==team && !actor->GetBool("bAdminOverride")){pump=actor;break;}
                    require(pump!=nullptr,"HeadBall team pump missing");
                    bool placed=false;
                    for(int i=0;i<16 && !placed;++i) {
                        float angle=float(i)*3.14159265f/8;
                        placed=pawn->SetLocation(pump->Location()+vec3(std::cos(angle)*65,std::sin(angle)*65,42));
                    }
                    require(placed,"no clear approach to team pump");
                    std::cout<<"OPEN_RUNE_HEADBALL_PUMP team="<<team<<std::endl;
                    }
                }
                if(headBallStartTick && !testHead && tick>=headBallStartTick+120) {
                    auto* pawn=slots[0]->pawn;
                    if(auto* weapon=pawn->GetUObject("Weapon"))CallEvent(pawn,"StowWeapon",{ExpressionValue::ObjectValue(weapon)});
                    UActor* head=nullptr;
                    auto* headClass=game.packages->GetPackage("RuneI")->GetClass("RagnarHead");
                    for(int height:{40,0,80})for(int direction=0;direction<8 && !head;++direction) {
                        float angle=direction*3.14159265f/4;
                        auto at=pawn->Location()+vec3(std::cos(angle)*75,std::sin(angle)*75,height);
                        head=pawn->Spawn(headClass,{},{},at,Rotator(0,0,0));
                    }
                    require(head!=nullptr,"head spawn failed");
                    testHead=UObject::TryCast<UActor>(CallEvent(head,"SpawnCopy",{ExpressionValue::ObjectValue(pawn)}).ToObject());require(testHead,"head copy failed");
                    CallEvent(pawn,"AcquireInventory",{ExpressionValue::ObjectValue(testHead)});
                    pawn->SetObject("Weapon",testHead);testHead->GotoState("Active","Begin");
                    std::cout<<"OPEN_RUNE_HEADBALL_HEAD equipped="<<testHead->Name.ToString()<<std::endl;
                }

                if(headBallStartTick && testHead && !headScored && !testHead->bDeleteMe() && testHead->GetUObject("LastThrower")==slots[0]->pawn && !testHead->bCarriedItem()) {
                    require(testHead && !testHead->bDeleteMe() && testHead->GetUObject("LastThrower")==slots[0]->pawn && !testHead->bCarriedItem(),"original head throw failed");
                    UActor* goal=nullptr;
                    for(auto* actor:game.Level->Actors)if(actor && actor->IsA("HeadBallZone") && (!actor->GetBool("bTeamOnly") || actor->GetByte("TeamNumber")==activeTeams[0])){goal=actor;break;}
                    require(goal!=nullptr,"HeadBall scoring zone missing");
                    headScoreExpected=goal->GetBool("bTeamBonus") && goal->GetByte("TeamNumber")==activeTeams[0]?int(goal->GetInt("TeamBonusScore")):int(goal->GetInt("ScoreAmount"));
                    bool entered=false;
                    for(int z=-1;z<=1 && !entered;++z)for(int x=-1;x<=1 && !entered;++x)for(int y=-1;y<=1 && !entered;++y) {
                        if(testHead->SetLocation(goal->Location()+vec3(x*32,y*32,z*32)) && testHead->FindRegion().Zone==goal)entered=true;
                    }
                    require(entered,"could not place thrown head in goal zone");const auto goalPoint=testHead->Location();testHead->UpdateActorZone();
                    require(testHead->bDeleteMe(),"goal did not consume head");
                    auto** teams=static_cast<UObject**>(game.GameInfo->GetProperty("Teams"));
                    require(teams[activeTeams[0]]->GetFloat("Score")==headScoreExpected && slots[0]->pawn->GetUObject("PlayerReplicationInfo")->GetFloat("Score")==headScoreExpected,"original head score differs");
                    headScored=true;std::cout<<"OPEN_RUNE_HEADBALL_GOAL points="<<headScoreExpected<<" consumed=1"<<std::endl;
                    // Exercise the shipped zone's optional limb rule with an
                    // actual thrown limb actor. Placement is a test fixture.
                    UActor* limb=nullptr;auto* limbClass=game.packages->GetPackage("RuneI")->GetClass("RagnarLArm");
                    for(int height:{40,0,80})for(int direction=0;direction<8 && !limb;++direction) {
                        float angle=direction*3.14159265f/4;
                        limb=slots[0]->pawn->Spawn(limbClass,{},{},slots[0]->pawn->Location()+vec3(std::cos(angle)*75,std::sin(angle)*75,height),{});
                    }
                    require(limb,"limb fixture spawn");limb->SetObject("LastThrower",slots[0]->pawn);limb->bCarriedItem()=false;
                    require(limb->SetLocation(goalPoint),"limb objective placement");limb->UpdateActorZone();
                    require(limb->bDeleteMe(),"goal did not consume limb");
                    headScoreExpected+=limbsScore?1:0;
                    require(teams[activeTeams[0]]->GetFloat("Score")==headScoreExpected,"limb scoring rule differs");
                    require(game.GameInfo->GetBool("bAllowLimbSever")==limbsScore,"limb severing rule differs");
                    std::cout<<"OPEN_RUNE_HEADBALL_LIMB points="<<(limbsScore?1:0)<<" consumed=1"<<std::endl;
                }
                if(headBallStartTick && tick==headBallStartTick+420) {
                    require(headScored,"network throw did not reach objective fixture");
                    CallEvent(slots[1]->pawn,"Died",{ExpressionValue::ObjectValue(slots[0]->pawn),ExpressionValue::NameValue("Decapitated"),ExpressionValue::VectorValue(slots[1]->pawn->Location())});
                    require(slots[0]->pawn->GetUObject("PlayerReplicationInfo")->GetFloat("Score")==headScoreExpected,"HeadBall kill incorrectly awarded points");
                    std::cout<<"OPEN_RUNE_HEADBALL_KILL no_points=1"<<std::endl;
                }
            }
            if(testArenaMode && fixtureRosterComplete) {
                // A bounded transform probe on an authored brush checks remote
                // visual/collision agreement even when this map's gates are idle.
                if(commandline->HasArg("","--rune-net-test-moving-brush") && tick==120)for(auto* actor:game.Level->Actors)if(auto* brush=GetMovingBrush(actor);brush && brush->Brush()) {
                    arenaTestBrush=brush;arenaBrushStart=brush->Location();
                    require(brush->SetLocation(arenaBrushStart+vec3(0,0,32)),"test brush move rejected");break;
                }
                if(tick==180 && arenaTestBrush)require(arenaTestBrush->SetLocation(arenaBrushStart),"test brush restore rejected");
                if(arenaDeathTick && tick==arenaDeathTick+180 && arenaKilledRound==4) {
                    auto* pawn=slots[3]->pawn;require(pawn->Health()<=0,"test respawn expected defeated player");
                    CallEvent(pawn,"ServerReStartPlayer");require(pawn->Health()==200,"original script respawn failed");
                    std::cout<<"OPEN_RUNE_ARENA_SCRIPT_RESPAWN player=4"<<std::endl;
                }
                if(tick==60) {
                    UActor* queueZone=nullptr;for(auto* actor:game.Level->Actors)if(actor && actor->IsA("QueueZone")){queueZone=actor;break;}
                    require(queueZone!=nullptr,"Arena map has no queue zone");
                    for(auto& slot:slots) {
                        bool placed=false;
                        // Find a standing surface inside the authored zone;
                        // its editor icon may float above a sloped boundary.
                        for(int z=0;z<=3 && !placed;++z)for(int radius=0;radius<=5 && !placed;++radius)
                            for(int x=-radius;x<=radius && !placed;++x)for(int y=-radius;y<=radius && !placed;++y) {
                                auto at=queueZone->Location()+vec3(x*64,y*64,z*64);vec3 floor,normal;
                                if(!slot->pawn->Trace(floor,normal,at-vec3(0,0,512),at,false,vec3(10,10,0)) || normal.z<.7f)continue;
                                at=floor+vec3(0,0,slot->pawn->CollisionHeight()+2);
                                if(slot->pawn->SetLocation(at) && slot->pawn->FindRegion().Zone && slot->pawn->FindRegion().Zone->IsA("QueueZone"))placed=true;
                            }
                        slot->pawn->Velocity()=slot->pawn->Acceleration()=vec3(0);
                        require(placed,"could not place test player inside queue zone");
                        slot->pawn->UpdateActorZone();
                    }
                    std::cout<<"OPEN_RUNE_ARENA_QUEUED players="<<slots.size()<<" team_size="<<game.GameInfo->GetInt("maxArenaTeam")<<std::endl;
                }
                if(game.GameInfo->GetByte("GameState")==1) {
                    if(!arenaFightTick)arenaFightTick=tick;
                    int matchNumber=game.GameInfo->GetInt("CurrentMatch");
                    if(tick>arenaFightTick+240 && arenaKilledRound!=matchNumber) {
                        UPlayerPawn* winner=nullptr;
                        for(auto& slot:slots)if(slot->pawn->GetUObject("PlayerReplicationInfo")->GetByte("Team")==0)winner=slot->pawn;
                        require(winner!=nullptr,"Arena has no champion");
                        for(auto& slot:slots)if(slot->pawn->GetUObject("PlayerReplicationInfo")->GetByte("Team")==1)
                            CallEvent(slot->pawn,"Died",{ExpressionValue::ObjectValue(winner),ExpressionValue::NameValue("Crushed"),ExpressionValue::VectorValue(slot->pawn->Location())});
                        arenaKilledRound=matchNumber;arenaDeathTick=tick;
                        std::cout<<"OPEN_RUNE_ARENA_DEFEAT round="<<matchNumber<<std::endl;
                    }
                } else arenaFightTick=0;
            }
            if(commandline->HasArg("","--rune-net-character-fixture") && tick==120) {
                for(auto& slot:slots)if(slot->joined && slot->pawn && slot->pawn->Health()>0)
                    CallEvent(slot->pawn,"Died",{ExpressionValue::ObjectValue(slot->pawn),ExpressionValue::NameValue("Crushed"),ExpressionValue::VectorValue(slot->pawn->Location())});
            }
            if(testTeam && fixtureRosterComplete) {
                if(tick==30) {
                    auto* a=slots[0]->pawn;auto* b=slots[2]->pawn;
                    std::cout<<"OPEN_RUNE_TEAM_AIM distance="<<length(a->Location()-b->Location())<<" height="<<a->Location().z-b->Location().z<<" yaw="<<a->Rotation().Yaw<<" view="<<a->ViewRotation().Yaw<<" desired="<<Rotator::FromVector(b->Location()-a->Location()).Yaw<<" state="<<a->GetStateName().ToString()<<" physics="<<int(b->Physics())<<std::endl;
                    allyPosition=slots[2]->pawn->Location();require(RuneKick::Target(slots[0]->pawn)==slots[2]->pawn,"team kick target");require(RuneKick::Start(slots[0]->pawn),"team kick start");}
                if(tick==100)require(RuneBash::Start(slots[0]->pawn),"team bash start");
                if(tick==200) {
                    require(slots[2]->pawn->Health()==100 && slots[2]->pawn->RuneBashStuns==0 && length(slots[2]->pawn->Location()-allyPosition)<2,"friendly kick/bash changed ally");
                    CallEvent(slots[2]->pawn,"JointDamaged",{ExpressionValue::IntValue(10),ExpressionValue::ObjectValue(slots[0]->pawn),
                        ExpressionValue::VectorValue(slots[2]->pawn->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
                    require(slots[2]->pawn->Health()==100,"friendly weapon damage was not blocked");
                    std::cout<<"OPEN_RUNE_TEAM_FRIENDLY_OK health=100 knockback=0 stagger=0"<<std::endl;
                }
                if(tick==240 || tick==360 || tick==480) {
                    size_t victim=tick==240?0:tick==360?1:3,killer=tick==240?0:2;
                    CallEvent(slots[victim]->pawn,"Died",{ExpressionValue::ObjectValue(slots[killer]->pawn),ExpressionValue::NameValue("Crushed"),ExpressionValue::VectorValue(slots[victim]->pawn->Location())});
                }
            }
            if(fixtureRosterComplete && commandline->HasArg("","--rune-net-test-feedback") && tick==240) {
                auto* a=slots[0]->pawn;auto* b=slots[1]->pawn;
                require(a && b,"feedback fixture needs two players");int before=b->Health();
                CallEvent(b,"JointDamaged",{ExpressionValue::IntValue(10),ExpressionValue::ObjectValue(a),ExpressionValue::VectorValue(b->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
                require(b->Health()<before,"feedback fixture damage");
                std::cout<<"OPEN_RUNE_FEEDBACK_FIXTURE health="<<b->Health()<<std::endl;
            }
            if(fixtureRosterComplete && commandline->HasArg("","--rune-net-test-match") && tick==120) {
                // Exercise the original suicide scoring callback, not a synthetic score edit.
                CallEvent(slots[0]->pawn,"Died",{ExpressionValue::ObjectValue(slots[0]->pawn),ExpressionValue::NameValue("Crushed"),ExpressionValue::VectorValue(slots[0]->pawn->Location())});
            }
            if(fixtureRosterComplete && ((testArena && tick==600) || (commandline->HasArg("","--rune-net-test-match") && tick==240)))
            {
                CallEvent(slots[1]->pawn,"Died",{ExpressionValue::ObjectValue(slots[testArena?0:2%slots.size()]->pawn),
                    ExpressionValue::NameValue("Crushed"),ExpressionValue::VectorValue(slots[1]->pawn->Location())});
                std::cout<<"OPEN_RUNE_TEST_DEATH health="<<slots[1]->pawn->Health()<<std::endl;
            }
            if(diagnostics)stageInputMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-stageStart).count();
            auto levelStart=diagnostics?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
            game.TotalTime+=dt;game.LevelInfo->TimeSeconds()+=dt;
            game.Level->Tick(dt,false);
            if(diagnostics){stageLevelMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-levelStart).count();stagePhysicsMs+=RunePhysics::Stats().stepMs;++stageTicks;}
            if(mode==GameMode::HeadBall && !headBallStartTick && game.GameInfo->GetBool("bGameStarted")) {
                headBallStartTick=tick;std::cout<<"OPEN_RUNE_HEADBALL_STARTED tick="<<tick<<std::endl;
            }
            for(auto cancelled:authority->drainCancelledActions())
                if(auto& slot=slots.at(cancelled.peer-1);slot->joined)require(slot->peer->decideAction(cancelled.id,Admission::Cancelled),"cancel decision overflow");
            // Same per-peer delivery as actor events; release what every joined peer has.
            for(auto& slot:slots)if(slot->joined)
                for(const auto& result:authority->pendingCombat()) {
                    if(slot->combatCursor>=result.id)continue;
                    auto local=result;local.id=slot->peer->lastCombatPublished()+1;
                    if(!slot->peer->publishCombat(local))break;
                    slot->combatCursor=result.id;
                }
            uint64_t delivered=0,combatFloor=UINT64_MAX;
            for(const auto& slot:slots)if(slot->joined)combatFloor=std::min(combatFloor,slot->combatCursor);
            for(const auto& result:authority->pendingCombat()) {
                if(result.id>combatFloor)break;
                delivered=result.id;++contacts;
                int attackerSlot=0,targetSlot=0;
                for(size_t s=0;s<slots.size();++s)if(slots[s]->pawn){auto ref=authority->identity(slots[s]->pawn);if(ref==result.attacker)attackerSlot=int(s+1);if(ref==result.target)targetSlot=int(s+1);}
                std::cout<<"OPEN_RUNE_CONTACT move="<<int(result.move)<<" contact="<<int(result.contact)<<" health="<<result.targetHealth
                    <<" attacker="<<attackerSlot<<" target="<<targetSlot<<std::endl;
            }
            if(delivered)require(authority->releaseCombatThrough(delivered),"combat retention failed");
            require(!authority->combatFaulted(),"combat stream fault");
            if(tick%2==0)
            {
                auto captureStart=diagnostics?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
                require(authority->capture(world),"world capture failed");
                for(size_t i=0;i<slots.size();++i)if(slots[i]->joined)require(slots[i]->peer->snapshots.capture(world,tick+1,authority->movementAck(i+1)),"snapshot enqueue failed");
                ++snapshots;
                if(diagnostics){stageSnapshotMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-captureStart).count();++stageSnapshots;}
            }
            for(auto& cue:authority->drainSounds())for(auto& slot:slots)if(slot->joined && slot->pawn) {
                vec3 at(float(cue.position[0])/8,float(cue.position[1])/8,float(cue.position[2])/8);
                if(length(at-slot->pawn->Location())<=cue.radius+200.f)slot->peer->publishSound(cue);
            }
            readScores();
            bool fragReached=fragLimit && (mode==GameMode::Arena?(state.arenaPhase==3 && state.arenaRound>uint32_t(fragLimit)):teamMode(mode)?
                std::any_of(activeTeams.begin(),activeTeams.end(),[&](int t){return state.teamScores[t]>=fragLimit;}):
                std::any_of(state.scores.begin(),state.scores.end(),[&](const auto& p){return p.frags>=fragLimit;}));
            if(fragReached || (timeLimit && (mode!=GameMode::HeadBall || headBallStartTick) && tick-headBallStartTick>=uint64_t(timeLimit)*60)) {
                endedAt=now;state.phase=MatchPhase::Intermission;
                int best=INT32_MIN;bool tied=false;
                for(const auto& p:state.scores) {
                    if(p.frags>best){best=p.frags;state.winner=p.player;tied=false;}
                    else if(p.frags==best)tied=true;
                }
                if(tied)state.winner=0;
                if(teamMode(mode)) {
                    state.winner=0;int bestTeamScore=INT32_MIN;state.winningTeam=255;
                    for(int t:activeTeams) {
                        if(state.teamScores[t]>bestTeamScore){bestTeamScore=state.teamScores[t];state.winningTeam=uint8_t(t);}
                        else if(state.teamScores[t]==bestTeamScore)state.winningTeam=255;
                    }
                }
                // Always publish the final health/death state, including odd ticks.
                require(authority->capture(world),"final world capture failed");
                for(auto& slot:slots)if(slot->joined)require(slot->peer->snapshots.capture(world,tick+2),"final snapshot failed");
                std::cout<<"OPEN_RUNE_MATCH_ENDED round="<<round<<" reason="<<(fragReached?"frag-limit":"time-limit")<<" winner="<<state.winner<<" team="<<int(state.winningTeam)<<" tick_hz="<<(now>roundStartedAt?double(tick)*1000/double(now-roundStartedAt):0)<<std::endl;
            }
        });
        state.secondsLeft=endedAt?uint32_t((endedAt+uint64_t(intermission)*1000-now+999)/1000):
            timeLimit?uint32_t(std::max<int64_t>(0,timeLimit-int64_t((mode==GameMode::HeadBall && !headBallStartTick?0:simulation.ticks()-headBallStartTick)/60))):0;
        // One small reliable record per player avoids overflowing Control's
        // 768-byte bound with a full named roster. Each peer caches sent labels.
        for(auto& recipient:slots)if(recipient->peer && recipient->peer->matchAgreed())
            for(size_t i=0;i<slots.size();++i)if(slots[i]->joined && !slots[i]->displayName.empty())
                if(!recipient->peer->publishPlayerLabel({uint32_t(i+1),slots[i]->displayName,slots[i]->bot}))break;
        for(auto& slot:slots)if(slot->peer && (!slot->publishedStatus || state!=*slot->publishedStatus))
            if(slot->peer->publishStatus(state))slot->publishedStatus=state;
        if(!lastStatus || state!=*lastStatus) {
            {lastStatus=state;std::cout<<"OPEN_RUNE_SCORE round="<<round<<" phase="<<int(state.phase);
                for(const auto& p:state.scores)std::cout<<" p"<<p.player<<"="<<p.frags<<","<<p.deaths;
                if(mode==GameMode::Arena) {
                    std::cout<<" arena="<<int(state.arenaPhase)<<","<<state.arenaRound<<","<<int(state.arenaCountdown);
                    for(const auto& p:state.scores)std::cout<<" role"<<p.player<<"="<<int(p.team)<<","<<int(p.queue);
                }
                if(mode==GameMode::HeadBall)std::cout<<" hb="<<state.headBallStarted<<","<<int(state.headBallCountdown)<<","<<int(state.readyTeams[0])<<","<<int(state.readyTeams[1]);
                if(teamMode(mode))std::cout<<" red="<<state.teamScores[0]<<" blue="<<state.teamScores[1];
                std::cout<<std::endl;}
        }
        for(auto& slot:slots)if(slot->session)slot->socket.send(*slot->session,slot->address);
        if(diagnostics && now>=nextDiagnostics) {
            nextDiagnostics=now+2000;auto timing=tickTimings.summary(nowMs());
            std::cout<<"OPEN_RUNE_SERVER_DIAGNOSTICS tick_samples="<<timing.count<<" tick_mean_ms="<<timing.mean
                <<" tick_p95_ms="<<timing.p95<<" tick_max_ms="<<timing.maximum<<" entities="<<world.size()
                <<" input_ms="<<stageInputMs/std::max(1u,stageTicks)<<" level_ms="<<stageLevelMs/std::max(1u,stageTicks)
                <<" physics_ms="<<stagePhysicsMs/std::max(1u,stageTicks)<<" snapshot_ms="<<stageSnapshotMs/std::max(1u,stageSnapshots)<<std::endl;
            stageInputMs=stageLevelMs=stagePhysicsMs=stageSnapshotMs=0;stageTicks=stageSnapshots=0;
            for(size_t i=0;i<slots.size();++i)if(slots[i]->joined && slots[i]->session)if(auto* stats=slots[i]->session->transportStats()) {
                auto snapshot=slots[i]->peer->snapshots.stats();
                std::cout<<"OPEN_RUNE_PEER_DIAGNOSTICS player="<<i+1<<" rtt_ms="<<stats->observedRttMs<<" rtt_samples="<<stats->rttSamples
                    <<" tx_timeouts="<<stats->losses<<" in_flight="<<stats->inFlight<<" pending="<<slots[i]->session->pending()
                    <<" snapshot_bytes="<<snapshot.latestBytes<<" snapshot_max_bytes="<<snapshot.maximumBytes
                    <<" snapshot_parts="<<snapshot.parts<<" snapshot_retries="<<snapshot.retries<<" snapshot_replaced="<<snapshot.replacements
                    <<" actor_lag="<<authority->actorWatermark()-slots[i]->actorCursor<<" actor_queue="<<authority->pendingActors().size()<<" bootstrap="<<slots[i]->bootstrap.size()<<std::endl;
            }
        }
        if(!idleWait)networkPause(1);
        else {
            // Sleep until a datagram, the next 60 Hz tick or a transport deadline.
            // Transport::poll sends one packet per step, so fresh datagrams, ticks
            // and queued sends keep the previous 1 ms cadence; only idle time grows.
            const auto after=nowMs();
            uint64_t waitMs=(hostActivity || simulation.ticks()!=ticksBefore)?1:10;
            waitSockets.clear();
            for(auto& slot:slots) {
                waitSockets.push_back(&slot->socket);
                if(slot->session)waitMs=std::min(waitMs,std::max<uint64_t>(1,slot->session->idleMs(after,waitMs)));
            }
            uint64_t waitUs=waitMs*1000;
            if(running && !endedAt)waitUs=std::min(waitUs,std::max<uint64_t>(250,simulation.untilNextTick(after*1000)));
            UdpSocket::waitAny(waitSockets,waitUs);
        }
    }
    for(size_t i=0;i<slots.size();++i)
    {authority->unbind(i+1);if(slots[i]->peer)slots[i]->peer->close(CloseReason::MatchEnded);}
    auto closeUntil=nowMs()+500;
    while(nowMs()<closeUntil)
    {
        for(auto& slot:slots)if(slot->session)
        {
            slot->socket.receive([&](const sockaddr_in& source,std::span<const uint8_t> bytes)
                {if(sameAddress(source,slot->address))slot->session->receiveDatagram(bytes);});
            slot->session->step(nowMs());slot->peer->step();slot->socket.send(*slot->session,slot->address);
        }
        networkPause(2);
    }
    std::cout<<"OPEN_RUNE_SERVER_STOP ticks="<<simulation.ticks()<<" snapshots="<<snapshots<<" actions="<<actions<<" contacts="<<contacts<<std::endl;
}

void RunRuneMultiplayerClient(Engine& game,const std::string& invitationFile)
{
    const bool embedded=bool(game.render);
    if(commandline->HasArg("","--rune-player-names"))RuneMultiplayer::ShowPlayerNames=commandline->GetArgInt("","--rune-player-names",1)!=0;
    if(!embedded && commandline->HasArg("","--snow"))RuneSnow::Mode=RuneSnow::ClampMode(commandline->GetArgInt("","--snow",-1));
    if(!embedded&&commandline->HasArg("","--rune-effects"))RuneVisualEffects::Enabled.store(commandline->GetArgInt("","--rune-effects",1)!=0);
    require(embedded || commandline->HasArg("","--save-directory"),"use a separate save directory for multiplayer");
    struct SessionReset {Engine& game;bool available,shown;~SessionReset(){
        game.NetworkInput={};game.NetworkOverlay={};game.NetworkFocusLost={};game.NetworkClient=false;
        RuneMultiplayer::Active=false;RuneMultiplayer::Leave=false;RuneMultiplayer::Respawn=false;
        RuneMenu::PlayerDead=false;RuneCombat::Ready=false;RuneBloodlust::ReadyMask=0;RuneMultiplayer::Waiting=false;
        game.console->bNoDrawWorld()=false;
        game.viewport->bWindowsMouseAvailable()=available;game.viewport->bShowWindowsMouse()=shown;
    }} reset{game,bool(game.viewport->bWindowsMouseAvailable()),bool(game.viewport->bShowWindowsMouse())};
    RuneMultiplayer::Active=true;RuneMultiplayer::Leave=false;RuneMultiplayer::Respawn=false;
    RuneCombat::Ready=false;RuneMenu::PlayerDead=false;
    auto invite=readInvite(invitationFile);RuneBattleLaws::MatchScope lawsScope{invite.laws};const uint32_t ownedSlot=invite.owned.id;
#ifndef __ANDROID__
    // Direct desktop joining also has a first-use prompt. Automated bounded
    // fixtures supply a name or retain their isolated Warrior fallback.
    if(!embedded && commandline->GetArgInt("","--rune-net-seconds",0)==0) {
        auto path=Directory::localAppData()/"SurrealEngine/MultiplayerName.txt";std::string name=commandline->GetArg("","--player-name",RuneMultiplayer::GetPlayerName());
        if(name.empty()){std::ifstream in(path);std::getline(in,name);}
        while(!validPlayerName(name)){name=trimPlayerName(TextInputDialog::TextInput(nullptr,"Choose a player name (1–24 characters). It is saved between matches.","Open Rune — Player name"));if(name.empty()){RuneMultiplayer::SetStatus("Joining cancelled");return;}}
        RuneMultiplayer::SetPlayerName(name);std::filesystem::create_directories(path.parent_path());std::ofstream out(path);out<<name;
    }
#endif
    sockaddr_in remote{};remote.sin_family=AF_INET;remote.sin_port=htons(invite.port);
    require(inet_pton(AF_INET,invite.address.c_str(),&remote.sin_addr)==1 && remote.sin_addr.s_addr!=INADDR_ANY,"invalid invitation address");
    game.NetworkClient=true;
    if(!embedded) {
    game.LoadEngineSettings();game.LoadKeybindings();
#ifndef __ANDROID__
    game.client->StartupFullscreen=false;
    game.client->WindowedViewportX=std::clamp(commandline->GetArgInt("","--rune-net-width",960),640,3840);
    game.client->WindowedViewportY=std::clamp(commandline->GetArgInt("","--rune-net-height",600),480,2160);
#endif
    game.OpenWindow();
    game.audiodev->InitDevice();game.render=std::make_unique<RenderSubsystem>(game.window->GetRenderDevice());
    }
    if(!embedded && commandline->HasArg("","--rune-net-fullscreen")) {
        require(commandline->HasArg("","--save-directory"),"fullscreen fixture requires isolated saves");
        game.window->ShowFullscreen();
    }
    game.window->SetWindowTitle("Open Rune — Joining multiplayer");
    auto url=game.GetDefaultURL(invite.map);url.Options.push_back(std::string("game=")+gameClass(invite.mode));game.LoadMap(url);
    game.LevelInfo->NetMode()=3;game.console->bNoDrawWorld()=true; // Loading overlay until the owned pawn exists.
    for(auto* actor:game.Level->Actors)if(actor && (actor->IsA("Pawn") || actor->IsA("Inventory") || actor->IsA("Projectile")))
    {game.Level->Collision.RemoveFromCollision(actor);actor->bHidden()=true;actor->NetworkReplica=true;}
    game.render->OnMapLoaded();
    for(auto* actor:game.Level->Actors)if(actor && actor->IsA("PlayerStart"))
    {game.CameraLocation=actor->Location()+vec3(0,0,40);game.CameraRotation=actor->Rotation();break;}
    if(!embedded)game.window->GetRenderDevice()->ResolutionScale=1.0f;
    game.SaveGameInfo.SaveGameSlot=DONT_SAVE_GAME;game.ClientTravelInfo={};
    // Let the route to the invited host select the outgoing interface. Binding
    // loopback works for local fixtures but cannot reach another machine.
    Socket socket("0.0.0.0",0,true);
    auto contract=MakeRuneMatchContract(game,invite.map,invite.players,invite.mode,invite.teams,invite.arenaTeamSize,invite.limbsScore);
    auto session=std::make_unique<SecureSession>(false,invite.key,invite.identity,addressId(remote));
    auto selectedName=commandline->GetArg("","--player-name",RuneMultiplayer::GetPlayerName());
    if(selectedName.empty())selectedName="Warrior "+std::to_string(ownedSlot);
    selectedName=trimPlayerName(selectedName);require(validPlayerName(selectedName),"Use a short player name with letters, numbers, spaces, apostrophe, dot, hyphen or underscore.");
    PlayerProfile profile{selectedName,false};
    auto peer=std::make_unique<ReplicationPeer>(*session,contract,RuneCharacters::Selected(),profile);RuneNetworkReplica replica;
    struct LocalAction { ActionKind kind; Input input; };
    std::array<bool,256> held{};std::deque<LocalAction> pending;
    double yaw=0,pitch=0;bool mouseReleased=false,connected=false;uint64_t lastBlock=0,lastJump=0;
    uint64_t feedbackUntil=0;std::string feedbackText;bool feedbackCaptured=false;
    uint32_t ownedLife=1;bool sawDeath=false,sawRespawn=false;
    UPlayerPawn* player=nullptr;std::string status="Joining match...",scoreText,scoreboard;
    std::optional<MatchStatus> reportedStatus;
    MovementPrediction prediction;
    PresentationTimeline presentation;
    const auto adaptiveEnabled=[&]{return commandline->HasArg("","--rune-net-smoothing")?
        commandline->GetArgInt("","--rune-net-smoothing",1)!=0:RuneMultiplayer::AdaptiveSmoothing.load();};
    bool smoothingWasOn=adaptiveEnabled();
    const bool predict=commandline->GetArgInt("","--rune-net-prediction",1)!=0;
    MovementPrediction::Step predictionStep=[&](MovementPose& p,const Input& i,const MovementAck& ack,float dt)
        {return StepRunePredictedMovement(game,player,p,i,ack,dt);};
    uint64_t predictedFrames=0,leadFrames=0,predictedAirFrames=0;double maxPredictionLead=0;
    ClientDiagnostics diagnostics;
    const bool diagnosticLog=commandline->HasArg("","--rune-net-diagnostics");
    bool diagnosticsWereOn=false;
    std::set<std::pair<uint32_t,uint32_t>> taggedPawns;
    auto intent=[&] {
        Input input;input.life=ownedLife;input.yaw=uint16_t(yaw);input.pitch=int16_t(pitch);
        auto pad=mouseReleased?RuneController::Axes{}:RuneController::Get();
        if(!mouseReleased && !RuneMultiplayer::Waiting.load()) {
            input.forward=int16_t(std::clamp(float(held[IK_W]||held[IK_Up])-float(held[IK_S]||held[IK_Down])+pad.forward,-1.0f,1.0f)*1000);
            input.strafe=int16_t(std::clamp(float(held[IK_D]||held[IK_Right])-float(held[IK_A]||held[IK_Left])+pad.strafe,-1.0f,1.0f)*1000);
            input.held=(held[IK_LeftMouse]||held[IK_Enter]?1:0)|(held[IK_RightMouse]||held[IK_RControl]?2:0)|(held[IK_C]?4:0);
        }
        return input;
    };
    auto queue=[&](ActionKind action){if(!RuneMultiplayer::Waiting.load() && connected && peer->status() && peer->status()->phase==MatchPhase::Playing && pending.size()<16)pending.push_back({action,intent()});};
    const bool scriptedLook=commandline->HasArg("","--rune-net-test-input") || commandline->HasArg("","--rune-net-stair-input") || commandline->HasArg("","--rune-net-native-fixture");
    game.viewport->bWindowsMouseAvailable()=true;
    auto release=[&]{replica.clearAnticipation();held.fill(false);pending.clear();mouseReleased=true;game.viewport->bShowWindowsMouse()=true;game.window->UnlockCursor();};
    game.NetworkFocusLost=release;
    game.NetworkInput=[&](EInputKey key,EInputType type,int delta)
    {
        if(key<0 || size_t(key)>=held.size())return;
        if(type==IST_Axis && !mouseReleased)
        {if(!scriptedLook){if(key==IK_MouseX)yaw+=double(delta)*32;if(key==IK_MouseY)pitch+=double(delta)*32;}return;}
        if(type==IST_Release){held[key]=false;if(key==IK_RightMouse || key==IK_RControl)queue(ActionKind::LowerShield);return;}
        if(type!=IST_Press || held[key])return;
        if(key==IK_Escape)
        {if(mouseReleased){mouseReleased=false;game.viewport->bShowWindowsMouse()=false;game.window->LockCursor();}else release();return;}
        if(mouseReleased || RuneMenu::Open.load() || RuneMultiplayer::Waiting.load())return;
        held[key]=true;
        if(key==IK_LeftMouse || (key==IK_Enter && player && player->Health()>0))queue((intent().held&2) && player && (player->Bloodlust.ready&2)?ActionKind::ShieldCharge:ActionKind::Attack);
        if(key==IK_RightMouse || key==IK_RControl)
        {auto now=nowMs();bool bash=player && !RuneDualWield::Equipped(player) && player->GetUObject("Shield");
            queue(bash && lastBlock && now-lastBlock<300?ActionKind::Bash:ActionKind::RaiseShield);lastBlock=now;}
        if(key==IK_F)queue(ActionKind::Kick);
        if(key==IK_Space) {
            auto now=nowMs();bool twice=lastJump && now-lastJump<=320;
            queue(twice?ActionKind::BloodLeap:ActionKind::Jump);lastJump=twice?0:now;
        }
        if(key==IK_E)queue(ActionKind::Powerup);
        if(key>=IK_1 && key<=IK_4)queue(ActionKind(uint8_t(ActionKind::SelectFists)+key-IK_1));
        if(key==IK_Ctrl || key==IK_LControl)queue(ActionKind::Use);
        if(key==IK_Q || key==IK_MiddleMouse)queue(ActionKind::Throw);
        if(key==IK_Enter && player && player->Health()<=0)queue(ActionKind::Respawn);
    };
    auto playerText=[&](uint32_t id,bool scoreboard){auto* label=peer->playerLabel(id);return label?labelText(*label,scoreboard):"Warrior "+std::to_string(id);};
    game.NetworkOverlay=[&]
    {
        if(RuneMultiplayer::Diagnostics.load() || diagnosticLog) {
            float y=18;
            for(const auto& text:diagnostics.lines) {
                auto size=game.render->GetTextSize(game.canvas->SmallFont(),text);
                float x=std::max(12.f,game.canvas->SizeX()-size.x-20),dx=0,dy=0;
                game.render->DrawText(game.canvas->SmallFont(),vec4(.95f,.83f,.55f,1),0,0,x,y,dx,dy,false,text,0,false);y+=size.y+6;
            }
        }
        if(player && peer->status()) {
            const bool teams=teamMode(invite.mode)||(invite.mode==GameMode::Arena&&invite.arenaTeamSize>1),names=RuneMultiplayer::ShowPlayerNames.load();
            float yaw=game.CameraRotation.YawRadians(),pitch=game.CameraRotation.PitchRadians();
            vec3 forward(std::cos(pitch)*std::cos(yaw),std::cos(pitch)*std::sin(yaw),std::sin(pitch));
            vec3 right(-std::sin(yaw),std::cos(yaw),0),up=cross(forward,right);
            float w=game.canvas->SizeX(),h=game.canvas->SizeY(),focal=w/(2*std::tan(game.CameraFovAngle*3.14159265f/360));
            for(const auto& score:peer->status()->scores) {
                auto* actor=replica.scoredPawn(score);
                if(!actor || actor==player || actor->bHidden() || actor->GetInt("Health")<=0 || (!teams&&!names))continue;
                auto at=actor->Location()+vec3(0,0,actor->CollisionHeight()+8);
                if(auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor))) {
                    auto bones=model->ActorJointMatrices(actor);auto world=GetRuneMeshToWorld(actor);
                    for(size_t j=0;j<model->Joints.size();++j)if(model->Joints[j].Name=="Head"){auto head=world*bones[j]*vec4(0,0,0,1);at=head.xyz()+vec3(0,0,8*actor->DrawScale());break;}
                }
                // Project with the renderer's actual scene matrix, including
                // pitch/roll/widescreen conventions, rather than a second camera.
                auto& frame=game.render->MainFrame.Frame;auto clip=frame.Projection*frame.WorldToView*vec4(at,1);
                if(clip.w<20 || clip.w>1800 || !player->FastTrace(at,game.CameraLocation))continue;
                float x=w*.5f+clip.x/clip.w*w*.5f,y=h*.5f+clip.y/clip.w*h*.5f-24,dx=0,dy=0;
                static const vec4 colors[]={vec4(1,.25f,.18f,1),vec4(.3f,.65f,1,1),vec4(.3f,1,.4f,1),vec4(1,.8f,.2f,1)};
                auto color=teams?colors[std::min<unsigned>(score.team,3)]:vec4(.95f,.91f,.81f,1);
                if(x<15||x>w-15||y<28||y>h-30)continue;
                if(names){auto text=playerText(score.player,false);auto size=game.render->GetTextSize(game.canvas->SmallFont(),text);float tx=x-size.x/2,ty=y;if(tx>=0&&tx+size.x<=w)game.render->DrawText(game.canvas->SmallFont(),color,0,0,tx,ty,dx,dy,false,text,0,false);}
                if(teams){
                    // Team shield remains even when names are hidden. Color and
                    // a letter identify all supported teams, including red/blue.
                    float my=names?y-16:y;vec3 q[]={vec3(x-8,my-6,0),vec3(x+8,my-6,0),vec3(x+7,my+3,0),vec3(x,my+9,0),vec3(x-7,my+3,0)};
                    for(int i=0;i<5;++i)game.render->Draw2DLine(color,0,q[i],q[(i+1)%5],false);
                    std::string mark(1,"RBGY"[std::min<unsigned>(score.team,3)]);auto z=game.render->GetTextSize(game.canvas->SmallFont(),mark);float mx=x-z.x/2,myText=names?y-18:y-8;game.render->DrawText(game.canvas->SmallFont(),color,0,0,mx,myText,dx,dy,false,mark,0,false);
                }
                if(diagnosticLog && taggedPawns.emplace(score.pawn.id,score.pawn.generation).second)
                    LogMessage("OPEN_RUNE_TEAM_TAG player="+std::to_string(score.player)+" pawn="+std::to_string(score.pawn.id)+" generation="+std::to_string(score.pawn.generation)+" team="+std::to_string(score.team)+" names="+std::to_string(names)+" text="+playerText(score.player,false));
            }
        }

#if defined(__ANDROID__) || defined(_WIN32)
        // Android also publishes these values to its overlay when Original is selected.
#ifdef _WIN32
        if (RuneHud::Enabled)
#endif
        {
            std::array<float,12> values{};
            if(player) {
                values[0]=1;values[1]=player->Health();values[2]=player->GetInt("MaxHealth");
                if(auto* shield=player->GetUObject("Shield");shield && !RuneDualWield::Equipped(player)) {
                    values[0]=3;values[3]=int32_t(shield->GetInt("Health"));
                    values[4]=shield->Class->GetDefaultObject<UObject>()->GetInt("Health");
                }
            }
            if(player && player->GetInt("MaxPower")>0){
                values[0]=float(unsigned(values[0])|4u);values[5]=player->GetInt("RunePower");values[6]=player->GetInt("MaxPower");
            }
            if(player && player->Health()>0) {
                values[0]=float(unsigned(values[0])|8u);values[7]=player->Bloodlust.rage;values[8]=100;
            }
            values[11]=VisualComparison::EngineFps.load();
            std::string message=status;
#ifdef _WIN32
            if(player && player->Health()<=0)message+="\nYou died — Enter to respawn after three seconds";
            if(mouseReleased)message+="\nControls released — Escape to resume";
            if(nowMs()<feedbackUntil)message+="\n"+feedbackText;
            if(game.window->GetKeyState(IK_Tab) || (peer->status() && peer->status()->phase==MatchPhase::Intermission))
                message+="\n"+scoreboard;
#endif
            std::lock_guard<std::mutex> lock(RuneHud::Mutex);
            RuneHud::Values=values;
            RuneHud::Messages=std::move(message);
            RuneHud::Score=scoreText;
            return;
        }
#endif
        auto draw=[&](float y,const std::string& text,vec4 color)
        {float x=20,dx=0,dy=0;game.render->DrawText(game.canvas->SmallFont(),color,0,0,x,y,dx,dy,false,text,0,false);};
        draw(18,"OPEN RUNE  /  MULTIPLAYER",vec4(0.91f,0.68f,0.30f,1));
        draw(44,status,vec4(.92f,.88f,.77f,1));
        if(player)draw(70,player->Health()<=0?"You died — Enter to respawn after three seconds":"Health "+std::to_string(player->Health())+"   Weapon: "+
            (player->GetUObject("Weapon")?player->GetUObject("Weapon")->Class->Name.ToString():"None")+"   Rune power "+std::to_string(player->GetInt("RunePower"))+"/"+std::to_string(player->GetInt("MaxPower")),vec4(.92f,.88f,.77f,1));
        if(nowMs()<feedbackUntil)draw(148,feedbackText,vec4(1,.7f,.3f,1));
        draw(96,mouseReleased?"Controls released — Escape to resume":"WASD move | Mouse look | LMB attack | RMB block (double tap: bash)",vec4(.85f,.79f,.65f,1));
        draw(122,"Space jump (2× + attack: heavy slam / rage leap) | Block+attack: shield rush | F kick | E power | Ctrl use | Q throw | 1–4 weapons",vec4(.85f,.79f,.65f,1));
        std::istringstream rows(scoreboard);float y=160;
        for(std::string row;std::getline(rows,row);y+=26)draw(y,row,vec4(.92f,.88f,.77f,1));
    };
    // The Android invitation page may have opened while the map loaded. Do not
    // overwrite its live pause/input state when the embedded client becomes ready.
    if(!embedded)RuneMenu::Open=false;
    if(!RuneMenu::Open.load())game.window->LockCursor();
    auto started=nowMs(),joinStarted=started,last=started,lastInput=uint64_t(0),lastSnapshot=uint64_t(0),spacing=uint64_t(33),playingAt=uint64_t(0),lastNonPlaying=started;
    auto seconds=commandline->GetArgInt("","--rune-net-seconds",0);
    auto maxFps=std::clamp(commandline->GetArgInt("","--maxfps",120),0,1000);
#ifdef __ANDROID__
    maxFps=0; // Swappy owns cadence on Android; never double-limit it.
#endif
    bool menuWasOpen=false,ended=false,handshakeAgreed=false;int targetFps=120;bool pacing=true;
    uint64_t fpsStart=started,fpsFrames=0,pendingSince=0,retryAt=0;
    // A world can arrive between a new actor's Spawn and Appearance events (separate lanes).
    // Validation precedes writes, so retry it, request a full world after 1 s, fail after 5 s.
    uint64_t installFailSince=0,installResyncAt=0;
    auto* device=game.window->GetRenderDevice();
    int clarity=embedded?VisualComparison::AppliedClarity.load():commandline->GetArgInt("","--clarity",0);
    int upscale=embedded?VisualComparison::AppliedUpscale.load():commandline->GetArgInt("","--upscale",0);
    if(!embedded) {
        device->WorldFeatures=commandline->GetArgInt("","--world-features",0);
        device->VisualEnhanced=commandline->GetArgInt("","--visual-enhanced",0)!=0;
        // Direct multiplayer bypasses the campaign render-settings loop.
        // Honor the same optional pack selection; embedded clients keep their UI choice.
        int quality=std::clamp(commandline->GetArgInt("","--texture-pack",0),0,2);
        if(device->TexturePackQuality!=quality){device->Flush(false);device->TexturePackQuality=quality;}
        VisualComparison::AppliedTexturePack=quality;
    }
    const float originalScale=1.0f;
    // The same fixed camera option as campaign profiling; never changes the
    // server or the player's saved view/settings.
    bool fixedView=commandline->HasArg("","--probe-view");vec3 probePosition{};Rotator probeRotation{};
    if(fixedView) {
        require(commandline->HasArg("","--save-directory"),"fixed camera requires isolated saves");
        auto value=commandline->GetArg("","--probe-view","");std::replace(value.begin(),value.end(),',',' ');
        std::istringstream input(value);int yaw=0,pitch=0;std::string trailing;
        require(bool(input>>probePosition.x>>probePosition.y>>probePosition.z>>yaw>>pitch) && !(input>>trailing) &&
            std::isfinite(probePosition.x) && std::isfinite(probePosition.y) && std::isfinite(probePosition.z) &&
            yaw>=-65536 && yaw<=65536 && pitch>=-16384 && pitch<=16384,"invalid fixed camera");
        probeRotation=Rotator(pitch,yaw,0);
    }
    const int profileFrames=std::clamp(commandline->GetArgInt("","--profile-frames",0),0,100000);
    uint64_t playingFrames=0;std::vector<double> frameTimes;
    double inputMs=0,networkMs=0,presentationMs=0,renderMs=0,pacingMs=0;
    auto milliseconds=[](auto from,auto to){return std::chrono::duration<double,std::milli>(to-from).count();};
    auto nextFrame=std::chrono::steady_clock::now();uint64_t frames=0,worlds=0,contacts=0,lastDraw=0;
    float pendingDrawTime=0;bool coveredWasOpen=false;
    const auto nativeFixture=commandline->GetArg("","--rune-net-native-fixture","");
    require(nativeFixture.empty() || nativeFixture=="dual" || nativeFixture=="blood" || nativeFixture=="bash","invalid native client fixture");
    const bool smoothLeapHandoff=commandline->GetArgInt("","--rune-net-leap-handoff",1)!=0;
    const bool smoothCameraBoom=commandline->GetArgInt("","--rune-net-camera-boom",1)!=0;
    const bool smoothStairs=commandline->GetArgInt("","--rune-net-stair-smoothing",1)!=0;
    const bool stairTest=commandline->HasArg("","--rune-net-stair-input");
    const int stairRunMs=std::clamp(commandline->GetArgInt("","--rune-net-stair-run-ms",3000),500,3000);
    const auto stairTracePath=commandline->GetArg("","--rune-net-stair-trace","");
    const auto stairCapturePath=commandline->GetArg("","--rune-net-stair-captures","");
    require((!stairTest && stairTracePath.empty() && stairCapturePath.empty()) || commandline->HasArg("","--save-directory"),"stair diagnostics require isolated saves");
    std::ofstream stairTrace;if(!stairTracePath.empty()) {
        stairTrace.open(stairTracePath);require(bool(stairTrace),"cannot write stair trace");
        stairTrace<<"ms,x,y,z,visual_z,camera_x,camera_y,camera_z,server_z,grounded,predicted,sequence,phase,pose_x,pose_z,replica_x,replica_z,camera_blocked,combat_phase,handoff\n";
    }
    std::ofstream motionTrace;
    if(commandline->HasArg("","--rune-net-motion-trace")) {
        require(commandline->HasArg("","--save-directory"),"motion trace requires isolated saves");
        motionTrace.open(commandline->GetArg("","--rune-net-motion-trace",""));require(bool(motionTrace),"cannot write motion trace");
        motionTrace<<"ms,owned,entity,life,grounded,mode,snapshot,tick,server_x,server_y,server_z,x,y,z,body,phase,delay_ms,buffered_ms\n";
    }
    uint64_t stairCaptureAt=0;unsigned stairCaptureIndex=0;
    const bool combatPlayback=commandline->GetArgInt("","--rune-combat-playback",1)!=0;
    std::ofstream animationTrace;
    if(commandline->HasArg("","--rune-net-animation-trace")) {
        require(commandline->HasArg("","--save-directory"),"animation trace requires isolated saves");
        animationTrace.open(commandline->GetArg("","--rune-net-animation-trace",""));
        require(bool(animationTrace),"cannot open animation trace");
        animationTrace<<"ms,entity,life,snapshot,body,body_frame,torso,torso_frame,auth_torso,rate,loop,preview,owned,left\n";
    }
    unsigned nativeSeen=0,nativeCaptured=0;
    const auto nativeCaptures=commandline->GetArg("","--rune-net-native-captures","");
    std::array<bool,12> nativeEvents{};
    std::array<bool,12> testEvents{};bool test=commandline->HasArg("","--rune-net-test-input");
    std::map<uint32_t,std::pair<vec3,Rotator>> moverStarts;std::set<uint32_t> movedBrushes;uint64_t moverChecks=0;
    uint64_t arenaDeathAt=0,arenaRespawnAt=0,headReadyAt=0,headPumpAt=0;
    bool arenaTestInput=commandline->HasArg("","--rune-net-test-arena-input");
    bool headBallTestInput=commandline->HasArg("","--rune-net-test-headball-input");
    bool teamTestInput=commandline->HasArg("","--rune-net-test-team-input");
    const bool sparringOpponent=commandline->HasArg("","--rune-net-sparring-opponent");
    const bool sparringDefender=commandline->HasArg("","--rune-net-sparring-defender");
    bool sparringBlock=false;
    uint64_t sparringNext=0;unsigned sparringMove=0;
    vec3 initial(0);double maxMovement=0;RuneNetworkCameraHeight cameraHeight;RuneNetworkCameraBoom cameraBoom;
    vec3 lastPredictedPosition{},lastPresentedVelocity{},predictionHandoff{};uint32_t lastPredictedLife=0;bool hadPredictedPosition=false,wasPredicting=false,wasLeaping=false;
    while(!game.quit && !RuneMultiplayer::Leave.load() && (!seconds || nowMs()-started<uint64_t(seconds)*1000))
    {
        auto frameStart=std::chrono::steady_clock::now();
        auto now=nowMs();float dt=float(std::min<uint64_t>(now-last,100))/1000.0f;last=now;
        game.TotalTime+=dt;game.LevelInfo->TimeSeconds()+=dt;
        bool menuOpen=RuneMenu::Open.load();
        const bool coveredMenu=menuOpen && RuneMenu::Covered.load() && !RuneDesktopMenu::Enabled(game);
        if(coveredMenu!=coveredWasOpen) {
            coveredWasOpen=coveredMenu;nextFrame=std::chrono::steady_clock::now();
            LogMessage(std::string("RUNE_MENU covered=")+(coveredMenu?"1":"0"));
        }
        if(menuOpen!=menuWasOpen) {
            replica.clearAnticipation();held.fill(false);pending.clear();mouseReleased=menuOpen;menuWasOpen=menuOpen;
            if(menuOpen)game.window->UnlockCursor();else game.window->LockCursor();
        }
        if(RuneMultiplayer::Respawn.exchange(false))queue(ActionKind::Respawn);
        int value=RuneMenu::Antialias.exchange(-1);if(value>=0)device->AntialiasMode=std::clamp(value,0,2);
        int target=RuneMenu::TargetFps.exchange(-1),pace=RuneMenu::Pacing.exchange(-1);
        if(target>=0)targetFps=target;if(pace>=0)pacing=pace!=0;
#ifdef __ANDROID__
        if(target>=0 || pace>=0)AndroidFramePacingConfigure(target,pace);
        maxFps=pacing?0:targetFps;
#else
        if(RuneDesktopMenu::Enabled(game)){maxFps=targetFps;device->UseVSync=pacing;}
#endif
        value=VisualComparison::RequestedMode.exchange(-1);if(value>=0)device->VisualEnhanced=value!=0;
        VisualComparison::AppliedMode=device->VisualEnhanced?1:0;
        value=VisualComparison::RequestedWorldFeatures.exchange(-1);if(value>=0)device->WorldFeatures=value&262143;
        VisualComparison::AppliedWorldFeatures=device->WorldFeatures;
        value=VisualComparison::RequestedTexturePack.exchange(-1);
        if(value>=0 && value!=device->TexturePackQuality){device->Flush(false);device->TexturePackQuality=std::clamp(value,0,2);}
        VisualComparison::AppliedTexturePack=device->TexturePackQuality;
        value=VisualComparison::RequestedClarity.exchange(-1);if(value>=0)clarity=value;
        constexpr float strength[]={0,.35f,.65f,.90f};
        device->AdaptiveSharpness=strength[clarity&3];device->ShadowLift=strength[(clarity>>3)&3];device->TextureAnisotropy=clarity&4?16:8;
        VisualComparison::AppliedClarity=clarity;
        value=VisualComparison::RequestedUpscale.exchange(-1);if(value>=0)upscale=std::clamp(value,0,3);
        device->UpscaleSGSR=upscale!=0;device->ResolutionScale=upscale==3?.25f:upscale==2?.5f:upscale==1?.75f:originalScale;
        VisualComparison::AppliedUpscale=upscale;
        // Poll live OS events every render frame. The callback consumes them
        // before any single-player command or VM combat event can execute.
        game.viewport->bShowWindowsMouse()=mouseReleased;
        RuneBloodlust::ReadyMask.store(player && player->Health()>0 && !ended?player->Bloodlust.ready:0);
        game.TickWindow();
        now=nowMs(); // Window dispatch can stall; never timestamp a new press before it actually ran.
        const bool diagnosticsOn=RuneMultiplayer::Diagnostics.load() || diagnosticLog;
        if(diagnosticsOn!=diagnosticsWereOn){diagnostics.reset();diagnosticsWereOn=diagnosticsOn;}
        if(smoothingWasOn!=adaptiveEnabled()){presentation.reset();smoothingWasOn=adaptiveEnabled();}
        auto networkStart=std::chrono::steady_clock::now();
        socket.receive([&](const sockaddr_in& source,std::span<const uint8_t> bytes)
        {if(sameAddress(source,remote))session->receiveDatagram(bytes);});
        if(auto name=RuneMultiplayer::GetPlayerName();!name.empty()&&name!=profile.name){profile.name=name;peer->setPlayerProfile(profile);}
        session->step(now,false);peer->step(false);
        if(peer->status() && peer->status()->weatherSeed && session->clockSync().ready(now)) {
            uint64_t age=RuneSnow::RemoteAge(peer->status()->weatherStartMs,now,session->clockSync().offsetMs());
            if(RuneSnow::timeline.seed!=peer->status()->weatherSeed && commandline->HasArg("","--rune-net-weather-trace"))
                std::cout<<"OPEN_RUNE_CLIENT_WEATHER seed="<<peer->status()->weatherSeed<<" start="<<peer->status()->weatherStartMs<<" age="<<age<<std::endl;
            RuneSnow::Observe(peer->status()->weatherSeed,age,game.TotalTime);
        }
        handshakeAgreed=handshakeAgreed || peer->matchAgreed() || peer->requestedTravel().has_value();
        if(peer->status())for(const auto& score:peer->status()->scores)if(score.player==ownedSlot && score.pawn.id)invite.owned=score.pawn;
        if(peer->requestedTravel()) {
            const auto travel=*peer->requestedTravel();
            require(travel.match.maxPlayers==invite.players && travel.match.mode==invite.mode && travel.match.map.starts_with(invite.mode==GameMode::HeadBall?"HB-":invite.mode==GameMode::Arena?"AR-":"DM-"),"invalid travel roster/map");
            status="Loading "+travel.match.map;RuneMultiplayer::SetStatus(status);RuneCombat::Ready=false;
            held.fill(false);pending.clear();prediction.reset();presentation.reset();cameraHeight.reset();cameraBoom.reset();hadPredictedPosition=false;replica=RuneNetworkReplica{};
            player=nullptr;game.viewport->Actor()=nullptr;game.CameraActor=nullptr;
            connected=false;ownedLife=1;headReadyAt=0;headPumpAt=0;playingAt=lastSnapshot=pendingSince=lastBlock=lastJump=0;testEvents.fill(false);reportedStatus.reset();diagnostics.reset();taggedPawns.clear();
            invite.map=travel.match.map;
            auto next=game.GetDefaultURL(invite.map);next.Options.push_back(std::string("game=")+gameClass(invite.mode));game.LoadMap(next);
            game.LevelInfo->NetMode()=3;game.console->bNoDrawWorld()=true;
            for(auto* actor:game.Level->Actors)if(actor && (actor->IsA("Pawn") || actor->IsA("Inventory") || actor->IsA("Projectile")))
            {game.Level->Collision.RemoveFromCollision(actor);actor->bHidden()=true;actor->NetworkReplica=true;}
            game.render->OnMapLoaded();game.SaveGameInfo.SaveGameSlot=DONT_SAVE_GAME;game.ClientTravelInfo={};
            if(!peer->acceptTravel(MakeRuneMatchContract(game,invite.map,invite.players,invite.mode,invite.teams,invite.arenaTeamSize,invite.limbsScore)))status="Cannot load next map: game files differ";
            last=nowMs();joinStarted=last;lastInput=0;
            std::cout<<"OPEN_RUNE_CLIENT_TRAVEL round="<<peer->mapEpoch()<<" map="<<invite.map<<" session="<<session->sessionId()<<std::endl;
        }
        // An interrupted client can return before the host retires its old
        // endpoint. Retry only an initial, unagreed handshake, never a live
        // replica or an explicit host rejection. The original 45 s join limit
        // remains the upper bound and Leave stays responsive during backoff.
        if(session->failed() && !connected && !handshakeAgreed &&
            !peer->matchMismatch() && !peer->remoteClosed() && now<joinStarted+45000) {
            status="Retrying connection… Check that the host and network are available.";
            if(!retryAt){retryAt=now+2000;LogMessage("OPEN_RUNE_CONNECTION handshake_retry=waiting");}
            if(now>=retryAt) {
                peer.reset();session=std::make_unique<SecureSession>(false,invite.key,invite.identity,addressId(remote));
                peer=std::make_unique<ReplicationPeer>(*session,contract,RuneCharacters::Selected(),profile);retryAt=0;diagnostics.reset();presentation.reset();
                LogMessage("OPEN_RUNE_CONNECTION handshake_retry=started");
            }
        } else if(session->failed() || peer->matchMismatch() || peer->remoteClosed())
        {status=peer->matchMismatch()?"Cannot join: game files or gameplay rules differ. Update the client and host to compatible releases.":peer->remoteClosed()?"The host ended the match. Ask for a new invitation.":"Connection lost. Reconnect to the network, then rejoin the match.";release();ended=true;RuneCombat::Ready=false;}
        if(!ended && !connected && peer->matchAgreed())status="Loading the match";
        if(!connected && now>=joinStarted+45000){status="Could not reach the match. Check the invitation and host.";release();ended=true;}
        const bool matchPlaying=peer->status() && peer->status()->phase==MatchPhase::Playing;
        // Lobby/intermission intentionally stop simulation snapshots. Give the
        // first playing world a full timeout after that quiet interval.
        if(!matchPlaying)lastNonPlaying=now;
        if(!ended && connected && matchPlaying && now>std::max(lastSnapshot,lastNonPlaying)+10000){status="Connection lost. Reconnect to the network, then rejoin the match.";release();ended=true;}
        if(ended){RuneMultiplayer::SetStatus(status);break;}
        require(!peer->actorOverflowed() && !peer->combatOverflowed(),"server event queue overflow");
        for(const auto& event:peer->drainActors())
        {
            if(event.operation==ActorOperation::Appearance)require(replica.appearance(game,event),"server appearance could not be applied");
            else if(event.operation==ActorOperation::Spawn)require(replica.spawn(game,event)!=nullptr,"server actor could not be created");
            else {
                if(event.ref==invite.owned){player=nullptr;game.viewport->Actor()=nullptr;ended=true;status="Your player left the match";}
                require(replica.unbind(event.ref),"unknown actor removal");
                // A newer delta already contains this removal. Request a full
                // world only if an in-flight candidate still references it.
            }
        }
        if(ended){RuneMultiplayer::SetStatus(status);break;}
        if(const auto* candidate=peer->world.candidate())
        {
            bool ready=peer->status() && std::any_of(peer->status()->scores.begin(),peer->status()->scores.end(),[&](const PlayerScore& score){return score.player==ownedSlot && score.pawn.id;}) && std::all_of(candidate->world.begin(),candidate->world.end(),[&](const Entity& e){return replica.actor(e.ref)!=nullptr;});
            if(!ready) {
                if(!pendingSince)pendingSince=now;
                // Lifecycle is reliable, snapshots expire independently. A
                // removal can overtake an old complete world on the other lane.
                if(replica.referencesRetired(*candidate) || (connected && now-pendingSince>1000)) {
                    std::cout<<"OPEN_RUNE_WORLD_RESYNC snapshot="<<candidate->id<<std::endl;
                    if(replica.referencesRetired(*candidate))peer->world.discardObsolete();else peer->world.resync();pendingSince=0;
                }
            } else pendingSince=0;
            bool installed=false;
            if(ready) {
                installed=replica.install(*candidate);
                if(installed) installFailSince=0;
                else {
                    if(!installFailSince){installFailSince=now;std::cout<<"OPEN_RUNE_WORLD_DEFERRED snapshot="<<candidate->id<<" reason="<<replica.rejection<<std::endl;}
                    require(now-installFailSince<=5000,"snapshot installation failed: "+replica.rejection);
                    if(now-installFailSince>1000 && now>=installResyncAt){std::cout<<"OPEN_RUNE_WORLD_RESYNC snapshot="<<candidate->id<<" reason=install"<<std::endl;peer->world.resync();installResyncAt=now+1000;}
                }
            }
            if(installed)
            {
                require(peer->world.commit(candidate->id),"snapshot commit failed");++worlds;
                if(diagnosticsOn)diagnostics.world(now);
                candidate=peer->world.installed(); // commit moves and destroys the pending candidate.
                if(smoothingWasOn)require(presentation.push(*candidate,now),"presentation timeline rejected installed world");
                if(lastSnapshot)spacing=std::clamp<uint64_t>(now-lastSnapshot,16,150);lastSnapshot=now;
                if(!connected)
                {
                    player=UObject::TryCast<UPlayerPawn>(replica.actor(invite.owned));require(player!=nullptr,"invitation has no owned player");
                    require(player->Class==game.packages->FindClass(openrune::Characters[RuneCharacters::Selected().character].className),"server spawned a different character");
                    std::cout<<"OPEN_RUNE_CHARACTER_VERIFIED class="<<player->Class->Name.ToString()<<" skin="<<int(RuneCharacters::Selected().skin)<<std::endl;
                    if(invite.mode==GameMode::Arena || invite.mode==GameMode::HeadBall)player->SetInt("MaxHealth",200);
                    game.viewport->Actor()=player;player->Player()=game.viewport;player->bBehindView()=true;
                    game.console->bNoDrawWorld()=false;
                    yaw=player->Rotation().Yaw;initial=player->Location();connected=true;RuneCombat::Ready=true;
                    game.window->SetWindowTitle("Open Rune — Multiplayer player "+std::to_string(ownedSlot));
                    std::cout<<"OPEN_RUNE_CLIENT_JOINED owned="<<invite.owned.id<<" actors="<<candidate->world.size()<<std::endl;
                }
                if(candidate->tick>2 && !playingAt)playingAt=now;
                for(const auto& entity:candidate->world)if(entity.ref==invite.owned)
                {
                    if(entity.health<=0)sawDeath=true;
                    if(entity.life!=ownedLife)
                    {ownedLife=entity.life;prediction.reset();yaw=entity.angles[1];pitch=0;pending.clear();held.fill(false);sawRespawn=true;
                        std::cout<<"OPEN_RUNE_CLIENT_RESPAWN life="<<ownedLife<<" health="<<entity.health<<std::endl;}
                }
                if(predict)prediction.checkpoint(*candidate,invite.owned,now,predictionStep);
            }
        }
        // A brief outage keeps its authenticated session-> Stop accumulating
        // actions/prediction while the world is stale; do not replay held combat
        // when the route returns. Lobby silence is normal, clock loss is not.
        const bool waiting=connected && (!session->ready() ||
            (matchPlaying && now>std::max(lastSnapshot,lastNonPlaying)+1500));
        if(RuneMultiplayer::Waiting.exchange(waiting)!=waiting) {
            held.fill(false);pending.clear();prediction.reset();lastBlock=lastJump=0;
            if(waiting)presentation.reset();
            LogMessage("OPEN_RUNE_CONNECTION waiting="+std::to_string(waiting)+" worlds="+std::to_string(worlds));
        }
        auto presentationStart=std::chrono::steady_clock::now();
        if(connected && !session->failed() && !peer->remoteClosed())
        {
            status=playingAt?std::string(modeName(invite.mode))+" — "+invite.map:"Waiting for players";
            if(headBallTestInput && player && peer->status()) {
                const auto& state=*peer->status();
                auto ownedScore=std::find_if(state.scores.begin(),state.scores.end(),[&](const PlayerScore& score){return score.player==ownedSlot;});
                if(ownedScore!=state.scores.end() && ownedScore->team<4 && !state.headBallStarted && !state.readyTeams[ownedScore->team] && now-headPumpAt>=1000) {
                    for(auto* actor:game.Level->Actors)if(actor && actor->IsA("HBGamePump") && actor->GetByte("TeamNumber")==ownedScore->team && !actor->GetBool("bAdminOverride")) {
                        if(length(actor->Location()-player->Location())<100) {yaw=Rotator::FromVector(actor->Location()-player->Location()).Yaw;queue(ActionKind::Use);headPumpAt=now;}break;
                    }
                }
                if(ownedSlot==1 && state.headBallStarted && !testEvents[1]) {
                    auto* weapon=player->GetUObject("Weapon");
                    if(weapon && weapon->IsA("Head")) {
                        if(!headReadyAt)headReadyAt=now;
                        if(now-headReadyAt>=800){queue(ActionKind::Throw);testEvents[1]=true;}
                    }
                }
            }
            if((arenaTestInput || headBallTestInput) && player) {
                if(player->Health()<=0){if(!arenaDeathAt)arenaDeathAt=now;
                    if(now-arenaDeathAt>3500 && now-arenaRespawnAt>1000){queue(ActionKind::Respawn);arenaRespawnAt=now;}}
                else arenaDeathAt=0;
            }
            if(teamTestInput && playingAt && ownedSlot==1 && !testEvents[0] && now-playingAt>=7500) {testEvents[0]=true;queue(ActionKind::Respawn);}
            if(!nativeFixture.empty() && playingAt) {
                mouseReleased=false;auto t=now-playingAt;
                auto press=[&](size_t i,uint64_t at,EInputKey key,bool down) {
                    if(!nativeEvents[i] && t>=at){nativeEvents[i]=true;if(down)game.OnWindowKeyDown(key);else game.OnWindowKeyUp(key);}
                };
                if(nativeFixture=="dual") {
                    for(size_t i=0;i<4;++i){press(i*2,2000+i*2000,IK_LeftMouse,true);press(i*2+1,2100+i*2000,IK_LeftMouse,false);}
                    press(8,11000,IK_RightMouse,true);press(9,11060,IK_RightMouse,false);
                    press(10,11160,IK_RightMouse,true);press(11,14000,IK_RightMouse,false);
                } else if(nativeFixture=="bash") {
                    if(ownedSlot==1) {
                        press(0,2000,IK_RightMouse,true);press(1,2060,IK_RightMouse,false);
                        press(2,2160,IK_RightMouse,true);press(3,2240,IK_RightMouse,false);
                    }
                } else if(ownedSlot==1) {
                    press(0,2000,IK_Space,true);press(1,2060,IK_Space,false);
                    press(2,2160,IK_Space,true);press(3,2240,IK_Space,false);
                } else {
                    press(0,2400,IK_RightMouse,true);press(1,2500,IK_LeftMouse,true);
                    press(2,2600,IK_LeftMouse,false);press(3,4000,IK_RightMouse,false);
                }
                unsigned seen=nativeSeen;bool physicalSeen=false;
                if(auto* installed=peer->world.installed())for(const auto& e:installed->world) {
                    if(e.physical.kind==1){seen|=256;physicalSeen=true;}
                    if(e.combat.offhand.id)seen|=1;if(e.combat.flags&1)seen|=2;
                    if(e.combat.guard>=200)seen|=4;if(e.combat.phase==RuneBloodlust::Leaping)seen|=8;
                    if(e.combat.phase==RuneBloodlust::Charging)seen|=16;if(e.combat.flags&4)seen|=32;
                    if(nativeFixture=="bash")if(auto* a=replica.actor(e.ref);a && a->AnimFrame()>.2f) {
                        if(a->AnimSequence()=="H3_DefendAttack")seen|=64;
                        if(a->AnimSequence()=="S1_painFront")seen|=128;
                    }
                }
                if((nativeSeen&256)&&!physicalSeen)seen|=512;
                if(seen!=nativeSeen){nativeSeen=seen;LogMessage("OPEN_RUNE_NATIVE_SEEN mask="+std::to_string(seen));}
            }
            if(test && playingAt)
            {
                mouseReleased=false;auto t=now-playingAt;
                auto press=[&](size_t i,uint64_t at,EInputKey key,bool down)
                {if(!testEvents[i] && t>=at){testEvents[i]=true;if(down)game.OnWindowKeyDown(key);else game.OnWindowKeyUp(key);}};
                if(ownedSlot==1){press(0,1200,IK_F,true);press(1,1300,IK_F,false);}
                press(2,5000,IK_S,true);press(3,6000,IK_S,false);
                press(4,6500,IK_Space,true);press(5,6600,IK_Space,false);
                if(ownedSlot==1){press(6,8000,IK_Q,true);press(7,8100,IK_Q,false);}
                if(ownedSlot==2){press(8,14500,IK_Enter,true);press(9,14600,IK_Enter,false);}
                press(10,16500,IK_W,true);press(11,17000,IK_W,false);
            }
            if(stairTest && playingAt && ownedSlot==1) {
                mouseReleased=false;held.fill(false);auto t=now-playingAt;
                held[IK_W]=t>=1500 && t<1500+stairRunMs;held[IK_S]=t>=5500 && t<5500+stairRunMs;
            }
            auto pad=mouseReleased?RuneController::Axes{}:RuneController::Get();
            yaw+=pad.yaw*30000.0*dt;pitch+=pad.pitch*30000.0*dt;
            yaw=std::fmod(yaw+65536.0,65536.0);pitch=std::clamp(pitch,-12000.0,12000.0);
            const auto* snapshot=peer->world.installed();
            auto entity=std::find_if(snapshot->world.begin(),snapshot->world.end(),[&](const Entity& e){return e.ref==invite.owned;});
            require(entity!=snapshot->world.end(),"owned player disappeared");
            // Character selection can relocate the fixture pawns after the
            // lobby snapshot. Aim the automated bash at its actual target;
            // do not rely on the reserved Ragnar spawn's initial camera yaw.
            if(nativeFixture=="bash" && playingAt) {
                auto target=std::find_if(snapshot->world.begin(),snapshot->world.end(),[&](const Entity& e){return e.kind==EntityKind::Pawn && e.ref!=invite.owned && e.health>0;});
                if(target!=snapshot->world.end()) {
                    vec3 delta(float(target->position[0]-entity->position[0]),float(target->position[1]-entity->position[1]),0);
                    if(length(delta)>.01f)yaw=uint16_t(Rotator::FromVector(delta).Yaw);
                }
            }
            // Explicit practice client only. Uses ordinary authenticated intents,
            // animation wind-ups and damage; never controls the human's pawn.
            if((sparringOpponent || sparringDefender) && playingAt)
            {
                mouseReleased=false;held.fill(false);pitch=0;
                auto target=std::find_if(snapshot->world.begin(),snapshot->world.end(),[&](const Entity& e){
                    return e.kind==EntityKind::Pawn && e.ref.id!=invite.owned.id;
                });
                if(!sparringNext)sparringNext=now+30000;
                if(entity->health<=0) {
                    if(now>=sparringNext){queue(ActionKind::Respawn);sparringNext=now+4000;}
                } else if(target!=snapshot->world.end() && target->health>0) {
                    vec3 delta(float(target->position[0]-entity->position[0])/8,
                        float(target->position[1]-entity->position[1])/8,0);
                    yaw=uint16_t(Rotator::FromVector(delta).Yaw);
                    float distance=length(delta);
                    held[IK_W]=sparringOpponent && distance>85 && distance<450;
                    if(sparringDefender) {
                        bool block=(now-playingAt)%16000<10000 && entity->shield.id!=0;
                        held[IK_RControl]=block;
                        if(block!=sparringBlock){queue(block?ActionKind::RaiseShield:ActionKind::LowerShield);sparringBlock=block;}
                    }
                    if(sparringOpponent && now>=sparringNext && distance<115) {
                        auto kind=sparringMove%3==0?ActionKind::Attack:sparringMove%3==1?ActionKind::Kick:ActionKind::Bash;
                        queue(kind);++sparringMove;sparringNext=now+4000;
                        std::cout<<"OPEN_RUNE_SPARRING_ATTACK kind="<<int(kind)<<std::endl;
                    }
                } else sparringNext=now+5000;
            }
            if(peer->status() && peer->status()->phase!=MatchPhase::Playing){held.fill(false);pending.clear();prediction.reset();}
            // Actions capture their own complete input on the event frame. A
            // block must not acquire the next 50 Hz sample's later timestamp.
            while(!pending.empty()) {
                auto action=pending.front();pending.pop_front();
                auto sequence=peer->inputs.capture(action.input,now);require(sequence.has_value(),"action context overflow");
                action.input.sequence=*sequence;
                if(predict)require(prediction.sample(action.input,now),"action prediction marker rejected");
                if(auto id=peer->inputs.act(action.kind,now)) {
                    bool preview=replica.anticipate(invite.owned,*id,action.kind,action.input,now);
                    if(commandline->HasArg("","--rune-net-input-trace") || !nativeFixture.empty())std::cout<<"OPEN_RUNE_LOCAL_PRESS id="<<*id<<" kind="<<int(action.kind)<<" preview="<<preview<<" at="<<now<<std::endl;
                } else std::cout<<"OPEN_RUNE_LOCAL_ACTION_LIMIT kind="<<int(action.kind)<<std::endl;
            }
            Input input=intent();
            if(predict)require(prediction.sample(input,now),"local prediction sample rejected");
            if(now>=lastInput+20)
            {
                auto sequence=peer->inputs.capture(input,now);require(sequence.has_value(),"input history overflow");lastInput=now;
                input.sequence=*sequence;
                if(predict)require(prediction.sample(input,now),"sent prediction marker rejected");
            }
            for(auto outcome:peer->drainCombat())require(replica.queueCombat(outcome,now),"combat dependency queue overflow");
            auto state=replica.flushCombat(now);require(state!=RuneNetworkReplica::CombatQueue::Fault,"combat dependencies did not arrive");
            if(state==RuneNetworkReplica::CombatQueue::Resync)peer->world.resync();
            for(auto outcome:replica.drainResolvedCombat())
            {
                ++contacts;std::cout<<"OPEN_RUNE_CLIENT_CONTACT move="<<int(outcome.move)<<" health="<<outcome.targetHealth<<std::endl;
                const auto* world=peer->world.installed();
                auto aliveReference=[&](EntityRef ref,uint32_t life){return world && std::any_of(world->world.begin(),world->world.end(),[&](const Entity& e){return e.ref==ref && e.life==life;});};
                if(!aliveReference(outcome.attacker,outcome.attackerLife) || (outcome.target.id && !aliveReference(outcome.target,outcome.targetLife)))continue;
                bool blocked=outcome.contact==CombatContact::Blocked;
                if(outcome.attacker==invite.owned || outcome.target==invite.owned) {
                    feedbackText=blocked?"Blocked":outcome.contact==CombatContact::Hit?(outcome.target==invite.owned?"Hit taken":"Hit confirmed"):"Miss";feedbackUntil=now+350;
                    if(outcome.move==CombatMove::Bash){feedbackText=blocked?"Bash blocked":outcome.contact==CombatContact::Hit?(outcome.target==invite.owned?"Shield bash taken":"Shield bash hit"):"Shield bash missed";feedbackUntil=now+650;}
                    if(outcome.target==invite.owned)RuneFeedback::Emit(blocked?GameFeedback::Event::ShieldBlock:GameFeedback::Event::Damage,.7f);
                    else if(outcome.contact!=CombatContact::Miss)RuneFeedback::Emit(outcome.move==CombatMove::Weapon?GameFeedback::Event::WeaponHit:outcome.move==CombatMove::Bash?GameFeedback::Event::Bash:GameFeedback::Event::Kick,blocked?.35f:1.f);
                }

            }
            for(auto result:peer->inputs.drainResults()) {
                replica.resolveAnticipation(result,now);
                std::cout<<"OPEN_RUNE_CLIENT_ACTION id="<<result.id<<" status="<<int(result.status)<<std::endl;
            }
            for(auto id:peer->inputs.drainUnconfirmed())std::cout<<"OPEN_RUNE_CLIENT_UNCONFIRMED id="<<id<<std::endl;
            auto localAlpha=std::clamp(double(now-lastSnapshot)/double(spacing),0.0,1.0);
            auto visual=presentation.sample(now);
            if(adaptiveEnabled() && visual.before && visual.after)
                replica.presentAdaptive(*visual.before,*visual.after,visual.alpha,invite.owned,localAlpha,
                    combatPlayback?double(now)/1000:-1);
            else replica.present(localAlpha,combatPlayback?double(now)/1000:-1);
            replica.presentAnticipation(now);
            if(animationTrace.is_open())for(const auto& visualEntity:snapshot->world) {
                auto* a=replica.actor(visualEntity.ref);
                auto* proxy=a && a->HasProperty("AnimProxy")?UObject::TryCast<UActor>(a->GetUObject("AnimProxy")):nullptr;
                if(visualEntity.kind!=EntityKind::Pawn || !a || !proxy || visualEntity.health<=0)continue;
                animationTrace<<std::setprecision(9)<<now<<','<<visualEntity.ref.id<<','<<visualEntity.life<<','<<snapshot->id<<','
                    <<visualEntity.bodyAnimation<<','<<a->AnimFrame()<<','<<visualEntity.torsoAnimation<<','<<proxy->AnimFrame()<<','
                    <<double(visualEntity.torsoPhase)/32767<<','<<double(visualEntity.torsoRate)/256<<','<<bool(visualEntity.flags&16)<<','
                    <<proxy->RuneCombatPreviewWeight<<','<<(visualEntity.ref==invite.owned)<<','<<bool(visualEntity.combat.flags&1)<<'\n';
            }
            if(arenaTestInput && peer->world.installed())for(const auto& e:peer->world.installed()->world)if(e.kind==EntityKind::Mover) {
                auto* a=replica.actor(e.ref);auto* brush=GetMovingBrush(a);
                require(brush && brush->Brush(),"replica lost authored brush");
                auto [initial,inserted]=moverStarts.emplace(e.ref.id,std::make_pair(a->Location(),a->Rotation()));
                if(length(initial->second.first-a->Location())>.25f || initial->second.second.Yaw!=a->Rotation().Yaw ||
                    initial->second.second.Pitch!=a->Rotation().Pitch || initial->second.second.Roll!=a->Rotation().Roll)movedBrushes.insert(e.ref.id);
                if(a->bCollideActors()) {
                    auto bounds=brush->Brush()->BoundingBox.transform(GetMovingBrushTransform(brush));
                    require(a->Collision.Inserted && length(bounds.center()-a->Collision.Location)<.01f,"replica brush collision does not follow visual transform");
                    ++moverChecks;
                }
            }
            auto replicaPosition=player->Location(),predictedPosition=replicaPosition;
            const bool predicting=predict && prediction.active();
            const bool walking=(entity->flags&3)==3 && entity->health>0 && !entity->physical.kind;
            const bool leaping=smoothLeapHandoff && entity->combat.phase==RuneBloodlust::Leaping && entity->health>0 && !entity->physical.kind;
            const bool smoothMotion=walking||leaping;
            MovementPose pose;
            pose.mode=leaping?MovementFalling:walking?MovementWalking:MovementUnsupported;
            for(int axis=0;axis<3;++axis){pose.position[axis]=player->Location()[axis];pose.velocity[axis]=player->Velocity()[axis];}
            if(predicting)pose=prediction.present(now,predictionStep);
            predictedPosition=vec3(pose.position[0],pose.position[1],pose.position[2]);
            const bool continuous=hadPredictedPosition && lastPredictedLife==entity->life && dt<=.2f &&
                length(predictedPosition-lastPredictedPosition)<160.f;
            // Leap is authoritative special motion, so prediction deliberately
            // stops. Preserve presentation continuity through that source change,
            // including landing, instead of rewinding to an older server sample.
            // Death, physics knockdowns, teleports and life changes reset normally.
            const bool handoff=continuous && smoothMotion &&
                (predicting!=wasPredicting || (smoothLeapHandoff && leaping!=wasLeaping));
            const bool leapHandoff=smoothLeapHandoff && handoff && (leaping||wasLeaping);
            if(!continuous || !smoothMotion)predictionHandoff=vec3(0);
            else if(handoff)predictionHandoff=lastPredictedPosition+
                (leapHandoff?lastPresentedVelocity*dt:vec3(0))-predictedPosition;
            // Continue the previous visual velocity for this frame rather than
            // freezing or partially retaining the backward pop. The same swept
            // presentation resolver clips this continuation against map geometry.
            if(!leapHandoff)predictionHandoff*=std::exp(-dt/.065f);
            for(int axis=0;axis<3;++axis)pose.position[axis]+=predictionHandoff[axis];
            // Remote interpolation may put the local pawn inside a riser.
            // Sweep from its last resolved local position, never from that
            // older interpolated point. Keep both timelines collision-safe.
            if(predicting || smoothMotion) {
                if(continuous)player->Location()=lastPredictedPosition;
                else cameraHeight.reset();
                PresentRunePredictedMovement(player,pose);
            }
            lastPredictedPosition=player->Location();lastPredictedLife=entity->life;
            lastPresentedVelocity=vec3(pose.velocity[0],pose.velocity[1],pose.velocity[2]);
            hadPredictedPosition=true;wasPredicting=predicting;wasLeaping=leaping;
            if(predicting) {
                ++predictedFrames;
                if(pose.mode==MovementFalling || pose.mode==3)++predictedAirFrames;
                vec3 confirmed(float(entity->position[0])/8,float(entity->position[1])/8,float(entity->position[2])/8);
                double lead=length(player->Location()-confirmed);maxPredictionLead=std::max(maxPredictionLead,lead);
                if(lead>1 && (input.forward || input.strafe))++leadFrames;
                player->Rotation().Yaw=int(yaw);
            }
            player->ViewRotation()=Rotator(int(pitch)&65535,int(yaw),0);
            game.CameraActor=player;game.CameraRotation=player->ViewRotation();
            float yawRadians=game.CameraRotation.YawRadians(),pitchRadians=game.CameraRotation.PitchRadians();
            vec3 direction(std::cos(pitchRadians)*std::cos(yawRadians),std::cos(pitchRadians)*std::sin(yawRadians),std::sin(pitchRadians));
            auto anchor=player->Location()+vec3(0,0,35);auto smoothAnchor=anchor;
            // Keep the mesh and eye on one presentation height. Smoothing only
            // the camera makes the capsule's discrete tread rises visibly bob
            // Ragnar against a steadier background. Collision remains exact.
            smoothAnchor.z=cameraHeight.update(anchor.z,entity->life,(entity->flags&3)==3 && entity->health>0,dt,smoothStairs?.045f:.08f);
            player->NetworkStairOffset=smoothStairs?smoothAnchor.z-anchor.z:0;

            auto desired=smoothAnchor-direction*185.0f;
            vec3 hit,normal;auto* blocker=player->Trace(hit,normal,desired,smoothCameraBoom?smoothAnchor:anchor,false,vec3(10));
            if(!smoothCameraBoom)game.CameraLocation=blocker?hit+normal*4.0f:desired;
            else {
                float clear=blocker?std::clamp(dot(smoothAnchor-hit,direction)-2.f,0.f,185.f):185.f,preview=clear;
                // Only anticipate grounded travel. Sample the actual next floor
                // heights so descending steps do not feed a flat extrapolation
                // into the camera probe. No actor/VM movement is performed.
                if(walking&&!leaping&&length(vec3(player->Velocity().xy(),0))>10){
                    for(float ahead:{.08f,.16f}){
                        auto at=smoothAnchor+vec3(player->Velocity().xy(),0)*ahead;
                        auto foot=at-vec3(0,0,35+player->CollisionHeight());vec3 floor,floorNormal;
                        if(!player->Trace(floor,floorNormal,foot-vec3(0,0,96),foot+vec3(0,0,64),false,vec3(10,10,0))||floorNormal.z<.7f)continue;
                        at.z=floor.z+player->CollisionHeight()+35+(smoothAnchor.z-anchor.z);
                        vec3 nextHit,nextNormal;
                        if(player->Trace(nextHit,nextNormal,at-direction*185.f,at,false,vec3(10)))
                            preview=std::min(preview,std::clamp(dot(at-nextHit,direction)-2.f,0.f,185.f));
                    }
                }
                auto arm=cameraBoom.update(clear,preview,entity->life,continuous,dt);
                game.CameraLocation=smoothAnchor-direction*arm;
            }
            if(stairTrace && playingAt)stairTrace<<now-playingAt<<','<<player->Location().x<<','<<player->Location().y<<','<<player->Location().z<<','<<player->Location().z+player->NetworkStairOffset<<','
                <<game.CameraLocation.x<<','<<game.CameraLocation.y<<','<<game.CameraLocation.z<<','<<float(entity->position[2])/8<<','<<((entity->flags&3)==3)<<','<<prediction.active()<<','<<player->AnimSequence().ToString()<<','<<player->AnimFrame()<<','<<predictedPosition.x<<','<<predictedPosition.z<<','<<replicaPosition.x<<','<<replicaPosition.z<<','<<bool(blocker)<<','<<int(entity->combat.phase)<<','<<handoff<<'\n';
            float distance=length(game.CameraLocation-anchor);
            player->SetBool("bClientSideAlpha",distance<70);player->SetFloat("ClientSideAlphaScale",std::clamp((distance-15.0f)/55.0f,0.0f,1.0f));
            game.CameraFovAngle=RuneView::Adjust(90,90,float(game.window->GetPixelWidth())/std::max(game.window->GetPixelHeight(),1),RuneView::FieldOfView.load(),RuneView::AutomaticAspect.load(),false);
            RuneView::AppliedFov=game.CameraFovAngle;
            maxMovement=std::max(maxMovement,double(length(player->Location()-initial)));
            if(motionTrace && playingAt)for(const auto& e:snapshot->world)if(e.kind==EntityKind::Pawn)if(auto* a=replica.actor(e.ref))
                motionTrace<<now-playingAt<<','<<(e.ref==invite.owned)<<','<<e.ref.id<<','<<e.life<<','<<bool(e.flags&2)<<','<<(e.ref==invite.owned?int(pose.mode):0)<<','<<snapshot->id<<','<<snapshot->tick<<','
                    <<e.position[0]/8.f<<','<<e.position[1]/8.f<<','<<e.position[2]/8.f<<','<<a->Location().x<<','<<a->Location().y<<','<<a->Location().z<<','<<e.bodyAnimation<<','<<a->AnimFrame()<<','
                    <<presentation.stats().targetDelayMs<<','<<presentation.stats().bufferedMs<<'\n';
        }
        replica.tickEffects(dt);
        if(fixedView){game.CameraLocation=probePosition;game.CameraRotation=probeRotation;}
        scoreText.clear();scoreboard.clear();
        if(peer->status()) {
            const auto& state=*peer->status();
            if(!reportedStatus || state!=*reportedStatus) {
                reportedStatus=state;std::cout<<"OPEN_RUNE_CLIENT_SCORE round="<<peer->mapEpoch()<<" phase="<<int(state.phase)<<" winner="<<state.winner<<" team="<<int(state.winningTeam);
                for(const auto& p:state.scores)std::cout<<" p"<<p.player<<"="<<p.frags<<","<<p.deaths;
                if(state.mode==GameMode::Arena)std::cout<<" arena="<<int(state.arenaPhase)<<","<<state.arenaRound<<","<<int(state.arenaCountdown);
                if(state.mode==GameMode::HeadBall)std::cout<<" hb="<<state.headBallStarted<<","<<int(state.headBallCountdown)<<","<<int(state.readyTeams[0])<<","<<int(state.readyTeams[1]);
                if(teamMode(state.mode))std::cout<<" red="<<state.teamScores[0]<<" blue="<<state.teamScores[1];
                std::cout<<std::endl;
            }
            require(state.mode==invite.mode,"server mode changed without a contract");
            auto ordered=state.scores;std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.frags!=b.frags?a.frags>b.frags:a.player<b.player;});
            const auto time=std::to_string(state.secondsLeft/60)+":"+(state.secondsLeft%60<10?"0":"")+std::to_string(state.secondsLeft%60);
            if(state.phase==MatchPhase::Intermission && state.mode==GameMode::Arena && !state.winner && !ordered.empty() && ordered.front().frags>0) {
                status="Joint winners:";for(const auto& score:ordered)if(score.frags==ordered.front().frags)status+=" "+playerText(score.player,false);
                status+=" — next map in "+std::to_string(state.secondsLeft)+"s";
            }
            else if(state.phase==MatchPhase::Intermission)status=(teamMode(state.mode)?(state.winningTeam==255?std::string("Draw"):std::string(teamName(state.winningTeam))+" team wins"):(state.winner?playerText(state.winner,false)+" wins":"Draw"))+" — next map in "+std::to_string(state.secondsLeft)+"s";
            else if(state.phase==MatchPhase::Playing)status=std::string(modeName(invite.mode))+" — "+invite.map+" · "+(state.secondsLeft?time:"No time limit")+" · "+(state.fragLimit?"Frag limit "+std::to_string(state.fragLimit):"No frag limit");
            else status="Waiting for players and map loading";
            if(state.mode==GameMode::Arena && state.phase==MatchPhase::Playing) {
                const char* phases[]={"Enter the queue to fight","Fight","Get ready","Round complete"};
                status="Arena — "+invite.map+" · Round "+std::to_string(std::max(1u,state.arenaRound-((state.arenaPhase==1 || state.arenaPhase==3) && state.arenaRound?1:0)))+" · "+phases[state.arenaPhase];
                if(state.arenaCountdown)status+=" ("+std::to_string(state.arenaCountdown)+"s)";
            }
            if(state.mode==GameMode::HeadBall && state.phase==MatchPhase::Playing) {
                status="HeadBall — "+invite.map+" · "+(state.headBallStarted?time:state.headBallCountdown?"Starting in "+std::to_string(state.headBallCountdown)+"s":"Use your team's pump to get ready");
            }
            scoreboard=(teamMode(state.mode)?"RED "+std::to_string(state.teamScores[0])+"    BLUE "+std::to_string(state.teamScores[1])+"    GREEN "+std::to_string(state.teamScores[2])+"    GOLD "+std::to_string(state.teamScores[3])+"\n":"")+std::string(state.mode==GameMode::Arena?"PLAYER     WINS / LOSSES":state.mode==GameMode::HeadBall?"PLAYER     POINTS / DEATHS":"PLAYER     FRAGS / DEATHS");
            for(const auto& p:ordered) {
                if(!p.pawn.id && p.frags==0 && p.deaths==0)continue;
                scoreboard+="\n"+std::string(p.player==ownedSlot?"> ":"  ")+playerText(p.player,true)+(p.team==255?"":" ("+std::string(state.mode==GameMode::Arena?(p.team==0?"Champion":"Challenger"):(teamName(p.team)))+")")+(state.mode==GameMode::Arena && p.queue!=255?" [Queue "+std::to_string(p.queue)+"]":"")+"       "+std::to_string(p.frags)+" / "+std::to_string(p.deaths);
                if(p.player==ownedSlot)scoreText=std::to_string(p.frags)+" · "+time;
            }
        }
        RuneMenu::PlayerDead=player && player->Health()<=0 && !sparringDefender && (!peer->status() || peer->status()->phase==MatchPhase::Playing);
        if(waiting)status="Connection interrupted — waiting for the host…";
        RuneMultiplayer::SetStatus(status+(scoreboard.empty()?"":"\n"+scoreboard));
        peer->flushInputs();session->flush();
        socket.send(*session,remote);
        game.viewport->SetViewportRect(0,0,game.window->GetPixelWidth(),game.window->GetPixelHeight());
        if(player) {
            // Replica pawns bypass Login/ClientSetMusic; use the authored level song.
            // No player/world scripts are executed locally to produce audio.
            if(!player->Song() && game.LevelInfo->GetUObject("Song")) {
                player->SetObject("Song",game.LevelInfo->GetUObject("Song"));
                player->SongSection()=game.LevelInfo->GetByte("SongSection");player->Transition()=MTRAN_Instant;
            }
            game.audiodev->SetViewport(game.viewport);
            for(const auto& cue:peer->drainSounds()) {
                auto* package=game.packages->GetPackage(cue.package);
                if(!package || cue.object>uint32_t(package->GetExportCount()))continue;
                auto* sound=UObject::TryCast<USound>(package->GetUObject(int(cue.object)));if(!sound)continue;
                auto* source=replica.actor(cue.source);
                vec3 at(float(cue.position[0])/8,float(cue.position[1])/8,float(cue.position[2])/8);
                int channel=int((cue.emitter+0x100000)*16+cue.channel);
                const bool played=game.audiodev->PlaySound(source,channel,sound,at,float(cue.volume)/256,cue.radius,float(cue.pitch)/256,(cue.channel&14)==4);
                if(commandline->HasArg("","--rune-net-audio-log"))LogMessage("OPEN_RUNE_SOUND played="+std::to_string(played)+" asset="+cue.package+"."+sound->Name.ToString());
            }
            game.UpdateAudio();
        }
        auto renderStart=std::chrono::steady_clock::now();
        if(diagnosticsOn && now>=diagnostics.nextReport) {
            // Map position and the step in front, for reporting places that need a jump.
            diagnostics.position.clear();
            if(player) {
                auto step=ProbeRuneStepAhead(game,player);std::ostringstream text;text<<std::fixed<<std::setprecision(0);
                text<<"Position "<<invite.map<<" "<<step.x<<" "<<step.y<<" "<<step.z<<std::setprecision(1)<<" / step ahead ";
                if(step.found)text<<step.ahead;else text<<"—";
                text<<" (limit "<<step.limit<<")";diagnostics.position=text.str();
            }
        }
        if(diagnosticsOn)diagnostics.report(now,session->transportStats(),smoothingWasOn?&presentation.stats():nullptr);
        // Keep receiving snapshots and updating the match while the native UI
        // covers it, without rendering a hidden native-resolution world at 144 Hz.
        const bool drawFrame=!coveredMenu || now>=lastDraw+100;
        pendingDrawTime+=dt;
        // This client intentionally does not tick gameplay/VM actors. Advance
        // visual-only muscles explicitly after authoritative presentation.
        RunePhysics::Tick(game.Level,dt,false);
        if(drawFrame){RunePhysics::PresentationScope characterPresentation;game.render->DrawGame(pendingDrawTime);pendingDrawTime=0;++frames;lastDraw=now;}
        if(drawFrame && !feedbackCaptured && nowMs()<feedbackUntil && commandline->HasArg("","--rune-net-feedback-capture")) {
            captureFrame(game,commandline->GetArg("","--rune-net-feedback-capture",""));feedbackCaptured=true;
        }
        auto renderEnd=std::chrono::steady_clock::now();
        if(drawFrame && !nativeCaptures.empty() && nativeSeen!=nativeCaptured) {
            nativeCaptured=nativeSeen;captureFrame(game,nativeCaptures+"/native-"+std::to_string(nativeSeen)+".ppm");
        }
        if(drawFrame && playingAt && !stairCapturePath.empty() && now-playingAt>=1400 && now-playingAt<=9000 && now>=stairCaptureAt+50) {
            stairCaptureAt=now;captureFrame(game,stairCapturePath+"/frame-"+std::to_string(10000+stairCaptureIndex++)+".ppm");
        }
        if(drawFrame)++fpsFrames;
        if(now>=fpsStart+500){VisualComparison::EngineFps=float(fpsFrames)*1000.0f/float(now-fpsStart);fpsFrames=0;fpsStart=now;}
        const int loopLimit=coveredMenu?60:maxFps;
        if(loopLimit>0)nextFrame+=std::chrono::microseconds(1000000/loopLimit);
        auto current=std::chrono::steady_clock::now();if(nextFrame<current)nextFrame=current;
        std::this_thread::sleep_until(nextFrame);
        if(diagnosticsOn && drawFrame)diagnostics.frames.add(nowMs(),milliseconds(frameStart,std::chrono::steady_clock::now()));
        if(!coveredMenu && playingAt && ++playingFrames>60 && profileFrames>0 && int(frameTimes.size())<profileFrames) {
            auto frameEnd=std::chrono::steady_clock::now();
            inputMs+=milliseconds(frameStart,networkStart);networkMs+=milliseconds(networkStart,presentationStart);
            presentationMs+=milliseconds(presentationStart,renderStart);renderMs+=milliseconds(renderStart,renderEnd);
            pacingMs+=milliseconds(renderEnd,frameEnd);frameTimes.push_back(milliseconds(frameStart,frameEnd));
            if(int(frameTimes.size())==profileFrames) {
                std::sort(frameTimes.begin(),frameTimes.end());
                double mean=(inputMs+networkMs+presentationMs+renderMs+pacingMs)/profileFrames;
                LogMessage("OPEN_RUNE_CLIENT_PROFILE frames="+std::to_string(profileFrames)+" fps="+std::to_string(1000/mean)+
                    " mean_ms="+std::to_string(mean)+" p95_ms="+std::to_string(frameTimes[(profileFrames-1)*95/100])+
                    " input_ms="+std::to_string(inputMs/profileFrames)+" network_ms="+std::to_string(networkMs/profileFrames)+
                    " presentation_ms="+std::to_string(presentationMs/profileFrames)+" render_ms="+std::to_string(renderMs/profileFrames)+
                    " pacing_ms="+std::to_string(pacingMs/profileFrames)+" entities="+std::to_string(peer->world.installed()->world.size())+
                    " camera="+std::to_string(game.CameraLocation.x)+","+std::to_string(game.CameraLocation.y)+","+
                    std::to_string(game.CameraLocation.z)+","+std::to_string(game.CameraRotation.Yaw)+","+std::to_string(game.CameraRotation.Pitch)+
                    " world_features="+std::to_string(device->WorldFeatures)+" msaa="+std::to_string(device->AntialiasMode)+
                    " scale="+std::to_string(device->ResolutionScale));
                if(sparringDefender) {
                    frameTimes.clear();inputMs=networkMs=presentationMs=renderMs=pacingMs=0;
                }
            }
        }
    }
    LogMessage("OPEN_RUNE_CONNECTION ended="+(ended?status:std::string("Left match")));
    if(!nativeFixture.empty())LogMessage("OPEN_RUNE_NATIVE_CLIENT fixture="+nativeFixture+" mask="+std::to_string(nativeSeen));
    if(!ended)RuneMultiplayer::SetStatus(RuneMultiplayer::Leave.load()?"Left match":"Match ended");
    if(!embedded)captureFrame(game,commandline->GetArg("","--capture-frame",""));
    game.window->UnlockCursor();
    peer->close(CloseReason::Leave);auto closeUntil=nowMs()+500;
    while(nowMs()<closeUntil && !session->failed())
    {
        socket.receive([&](const sockaddr_in& source,std::span<const uint8_t> bytes)
            {if(sameAddress(source,remote))session->receiveDatagram(bytes);});
        session->step(nowMs());peer->step();socket.send(*session,remote);
        networkPause(2);
    }
    const auto& predictionStats=prediction.stats();
    std::cout<<"OPEN_RUNE_SMOOTHING adaptive="<<adaptiveEnabled()<<" delay_ms="<<presentation.stats().targetDelayMs
        <<" buffered_ms="<<presentation.stats().bufferedMs<<" holds="<<presentation.stats().holds<<" resets="<<presentation.stats().resets<<" catchups="<<presentation.stats().catchups<<std::endl;
    std::cout<<"OPEN_RUNE_PREDICTION enabled="<<predict<<" frames="<<predictedFrames<<" lead_frames="<<leadFrames
        <<" max_lead="<<maxPredictionLead<<" checkpoints="<<predictionStats.checkpoints<<" replay_steps="<<predictionStats.replayedSteps
        <<" max_correction="<<predictionStats.maxCorrection<<" resets="<<predictionStats.resets<<" bounded="<<predictionStats.limited<<" unsupported="<<predictionStats.unsupported<<" missing="<<predictionStats.missing<<" stale="<<predictionStats.stale<<" future="<<predictionStats.future<<" air_frames="<<predictedAirFrames<<std::endl;
    if(arenaTestInput)std::cout<<"OPEN_RUNE_BRUSH_CHECKS authored="<<moverStarts.size()<<" moved="<<movedBrushes.size()<<" checks="<<moverChecks<<std::endl;
    std::cout<<"OPEN_RUNE_CLIENT_STOP frames="<<frames<<" worlds="<<worlds<<" contacts="<<contacts<<" movement="<<maxMovement
        <<" death="<<sawDeath<<" respawn="<<sawRespawn<<" life="<<ownedLife<<std::endl;
    if(test)require(playingAt && worlds>30 && frames>60 && maxMovement>20,"interactive validation did not move the owned pawn");
    if(test && ownedSlot==2)require(sawDeath && sawRespawn && ownedLife==2,"interactive death and respawn validation failed");
}

// Automated client uses the production authenticated transport and replica
// installer, with no renderer or campaign ticks. Its brain has no authority.
void RunRuneMultiplayerBot(Engine& game,const std::string& invitationFile)
{
    require(commandline->HasArg("","--save-directory"),"bot needs isolated saves");
    auto invite=readInvite(invitationFile);RuneBattleLaws::MatchScope lawsScope{invite.laws};const uint32_t ownedSlot=invite.owned.id;
    const bool motionOnly=commandline->HasArg("","--rune-net-bot-motion");
    const bool scripted=motionOnly || commandline->HasArg("","--rune-net-bot-stress") || commandline->HasArg("","--rune-net-bot-idle") || commandline->HasArg("","--rune-net-test-headball-input");
    const uint32_t botSeed=uint32_t(commandline->GetArgInt("","--probe-seed",int(nowMs()))) ^ (ownedSlot*0x9e3779b9u);
    if(!commandline->HasArg("","--character") && !commandline->HasArg("","--character-skin") && !scripted) {
        std::mt19937 random(botSeed);std::vector<openrune::CharacterChoice> choices;
        for(size_t i=0;i<openrune::Characters.size();++i) {
            try { if(!game.packages->FindClass(openrune::Characters[i].className))continue; }
            catch(const std::exception&) { continue; }
            for(uint8_t skin=0;skin<openrune::Characters[i].skins;++skin)choices.push_back({uint8_t(i),skin});
        }
        require(!choices.empty(),"bot has no installed character models");
        auto choice=choices[random()%choices.size()];RuneCharacters::Multiplayer.store(choice.character|(choice.skin<<8));
    }
    RuneBot::Brain brain(botSeed,commandline->GetArgInt("","--rune-bot-difficulty",1));
    brain.debug=commandline->HasArg("","--rune-bot-debug");brain.limbsScore=invite.limbsScore;
    game.NetworkClient=true;
    auto load=[&] {
        auto url=game.GetDefaultURL(invite.map);url.Options.push_back(std::string("game=")+gameClass(invite.mode));game.LoadMap(url);
        game.LevelInfo->NetMode()=3;
        for(auto* a:game.Level->Actors)if(a && (a->IsA("Pawn") || a->IsA("Inventory") || a->IsA("Projectile"))) {
            game.Level->Collision.RemoveFromCollision(a);a->bHidden()=true;a->NetworkReplica=true;
        }
        game.SaveGameInfo.SaveGameSlot=DONT_SAVE_GAME;game.ClientTravelInfo={};
    };
    load();
    Socket socket("0.0.0.0",0,true);sockaddr_in remote{};remote.sin_family=AF_INET;remote.sin_port=htons(invite.port);
    require(inet_pton(AF_INET,invite.address.c_str(),&remote.sin_addr)==1,"invalid bot address");
    SecureSession session(false,invite.key,invite.identity,addressId(remote));
    ReplicationPeer peer(session,MakeRuneMatchContract(game,invite.map,invite.players,invite.mode,invite.teams,invite.arenaTeamSize,invite.limbsScore),RuneCharacters::Selected(),PlayerProfile{botName(ownedSlot),true});
    RuneNetworkReplica replica;
    auto start=nowMs(),lastInput=uint64_t(0),lastAction=uint64_t(0),lastWorld=start,lastNonPlaying=start;
    const int seconds=commandline->GetArgInt("","--rune-net-seconds",30);
    require(seconds>=0 && seconds<=86400 && (!scripted || seconds>0),"scripted test clients need a bounded duration");
    std::ofstream poseTrace;
    if(commandline->HasArg("","--rune-net-pose-trace")) {poseTrace.open(commandline->GetArg("","--rune-net-pose-trace",""));poseTrace<<"now,snapshot,tick,entity,life,health,x,y,z,vz,body,phase,rate,torso,torso_phase,flags\n";}
    const bool idleWait=commandline->GetArgInt("","--rune-net-idle-wait",1)!=0; // 0: previous fixed 2 ms poll.
    uint64_t loops=0;
    // Same Spawn/Appearance lane race as the client: retry, resync after 1 s, fail after 5 s.
    uint64_t botInstallFailSince=0,botInstallResyncAt=0,botUnboundSince=0,botUnboundReport=0;
    uint64_t botStallStart=0,botStallSeconds=0;bool botStalled=false;
    if(auto stall=commandline->GetArg("","--rune-net-bot-stall","");!stall.empty()) {
        const auto comma=stall.find(',');require(comma!=std::string::npos,"--rune-net-bot-stall=START,SECONDS");
        botStallStart=std::stoull(stall.substr(0,comma));botStallSeconds=std::stoull(stall.substr(comma+1));
        require(botStallSeconds>0 && botStallSeconds<=25,"bot stall must be 1..25 seconds");
    }
    auto botInstall=[&](const Snapshot& candidate,uint64_t now) {
        if(replica.install(candidate)){botInstallFailSince=0;return true;}
        if(!botInstallFailSince){botInstallFailSince=now;std::cout<<"OPEN_RUNE_BOT_WORLD_DEFERRED snapshot="<<candidate.id<<" reason="<<replica.rejection<<std::endl;}
        require(now-botInstallFailSince<=5000,"bot snapshot install: "+replica.rejection);
        if(now-botInstallFailSince>1000 && now>=botInstallResyncAt){peer.world.resync();botInstallResyncAt=now+1000;}
        return false;
    };
    uint64_t worlds=0,hbStartedAt=0,joinedAt=0,maxWorldGap=0,activeWorlds=0,activeMs=0,activeMaxGap=0;std::array<uint64_t,8> worldGaps{},activeGaps{};bool joined=false,thrown=false,lastWorldPlaying=false,selectedSkinSeen=false;std::optional<MatchStatus> prior;
    while(!seconds || nowMs()-start<uint64_t(seconds)*1000) {
        auto now=nowMs();bool botActivity=false;++loops;
        const int leaveAfter=commandline->GetArgInt("","--rune-net-leave-after",0);
        if(leaveAfter>0 && joinedAt && now-joinedAt>=uint64_t(leaveAfter)*1000)break;
        // Test only: --rune-net-bot-stall=START,SECONDS stops reading and acknowledging
        // the host for SECONDS, START seconds after joining, like a congested relayed link.
        if(botStallSeconds>0 && joinedAt && now>=joinedAt+botStallStart*1000) {
            if(now<joinedAt+(botStallStart+botStallSeconds)*1000) {
                if(!botStalled){botStalled=true;std::cout<<"OPEN_RUNE_BOT_STALL_BEGIN seconds="<<botStallSeconds<<std::endl;}
                std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;
            }
            if(botStalled){botStalled=false;botStallSeconds=0;lastWorld=lastNonPlaying=now;std::cout<<"OPEN_RUNE_BOT_STALL_END"<<std::endl;}
        }
        socket.receive([&](const sockaddr_in& source,std::span<const uint8_t> bytes){if(sameAddress(source,remote))session.receiveDatagram(bytes);});
        session.step(now);peer.step();
        if(peer.status())for(const auto& score:peer.status()->scores)if(score.player==ownedSlot && score.pawn.id)invite.owned=score.pawn;
        require(!peer.matchMismatch(),"bot game files or Laws of Battle differ from host");
        require(!session.failed(),"bot connection failed");
        if(peer.remoteClosed())break;
        if(peer.requestedTravel()) {
            auto travel=*peer.requestedTravel();invite.map=travel.match.map;
            replica=RuneNetworkReplica{};{auto previous=brain;brain=RuneBot::Brain(botSeed,commandline->GetArgInt("","--rune-bot-difficulty",1));brain.carryCounters(previous);}brain.debug=commandline->HasArg("","--rune-bot-debug");brain.limbsScore=invite.limbsScore;load();joined=false;thrown=false;hbStartedAt=0;prior.reset();lastWorld=now;
            require(peer.acceptTravel(MakeRuneMatchContract(game,invite.map,invite.players,invite.mode,invite.teams,invite.arenaTeamSize,invite.limbsScore)),"bot travel contract");
            std::cout<<"OPEN_RUNE_BOT_TRAVEL epoch="<<peer.mapEpoch()<<" map="<<invite.map<<std::endl;
        }
        require(!peer.actorOverflowed() && !peer.combatOverflowed(),"bot event overflow");
        for(const auto& e:peer.drainActors()) {
            if(e.operation==ActorOperation::Appearance)require(replica.appearance(game,e),"bot appearance");
            else if(e.operation==ActorOperation::Spawn)require(replica.spawn(game,e)!=nullptr,"bot actor spawn id="+std::to_string(e.ref.id)+" kind="+std::to_string(int(e.kind))+" class="+e.package+"."+e.actorClass+" map_actor="+e.mapActor);
            else require(replica.unbind(e.ref),"bot actor despawn");
        }
        if(auto* candidate=peer.world.candidate()) {
            // A snapshot waits until every entity's reliable spawn has arrived. Name a long wait.
            size_t unbound=0;const Entity* firstUnbound=nullptr;
            for(const auto& e:candidate->world)if(!replica.actor(e.ref)){if(!unbound++)firstUnbound=&e;}
            if(!unbound)botUnboundSince=0;
            else if(!botUnboundSince){botUnboundSince=now;botUnboundReport=now+2000;}
            else if(now>=botUnboundReport && (botUnboundReport=now+2000))
                std::cout<<"OPEN_RUNE_BOT_WORLD_WAITING snapshot="<<candidate->id<<" unbound="<<unbound<<" first="<<firstUnbound->ref.id<<" kind="<<int(firstUnbound->kind)<<" waited_ms="<<now-botUnboundSince<<std::endl;
            if(replica.referencesRetired(*candidate))peer.world.discardObsolete();
            else if(peer.status() && std::any_of(peer.status()->scores.begin(),peer.status()->scores.end(),[&](const PlayerScore& score){return score.player==ownedSlot && score.pawn.id;}) && std::all_of(candidate->world.begin(),candidate->world.end(),[&](const Entity& e){return replica.actor(e.ref)!=nullptr;}) && botInstall(*candidate,now)) {
                require(peer.world.commit(candidate->id),"bot snapshot commit");
                if(joined) {
                    const auto gap=now-lastWorld;maxWorldGap=std::max(maxWorldGap,gap);
                    const std::array<uint64_t,7> limits={100,250,500,1000,2000,5000,15000};
                    ++worldGaps[std::upper_bound(limits.begin(),limits.end(),gap)-limits.begin()];
                    if(lastWorldPlaying && peer.status() && peer.status()->phase==MatchPhase::Playing) {
                        ++activeWorlds;activeMs+=gap;activeMaxGap=std::max(activeMaxGap,gap);
                        ++activeGaps[std::upper_bound(limits.begin(),limits.end(),gap)-limits.begin()];
                    }
                }
                lastWorldPlaying=peer.status() && peer.status()->phase==MatchPhase::Playing;
                ++worlds;lastWorld=now;
                if(poseTrace)for(const auto& e:peer.world.installed()->world)if(e.kind==EntityKind::Pawn)poseTrace<<now<<','<<peer.world.installed()->id<<','<<peer.world.installed()->tick<<','<<e.ref.id<<','<<e.life<<','<<e.health<<','<<e.position[0]<<','<<e.position[1]<<','<<e.position[2]<<','<<e.velocity[2]<<','<<e.bodyAnimation<<','<<e.animationPhase<<','<<e.animationRate<<','<<e.torsoAnimation<<','<<e.torsoPhase<<','<<int(e.flags)<<'\n';
                if(!joined) {
                    if(!joinedAt)joinedAt=now;
                    auto* chosen=replica.actor(invite.owned);require(chosen!=nullptr,"bot owned pawn missing");
                    require(chosen->Class==game.packages->FindClass(openrune::Characters[RuneCharacters::Selected().character].className),"bot received wrong character");
                    std::cout<<"OPEN_RUNE_CHARACTER_VERIFIED class="<<chosen->Class->Name.ToString()<<" skin="<<int(RuneCharacters::Selected().skin)<<std::endl;joined=true;
                    std::cout<<"OPEN_RUNE_BOT_JOINED owned="<<invite.owned.id<<" map="<<invite.map<<" entities="<<peer.world.installed()->world.size()<<" join_ms="<<now-start<<std::endl;
                }
            }
        }
        const bool matchPlaying=peer.status() && peer.status()->phase==MatchPhase::Playing;
        if(!matchPlaying)lastNonPlaying=now;
        require(now-(joined?std::max(lastWorld,lastNonPlaying):lastWorld)<(joined?10000u:45000u),"bot world timeout");
        if(joined && now>=lastInput+20) {
            auto* snapshot=peer.world.installed();
            auto entity=std::find_if(snapshot->world.begin(),snapshot->world.end(),[&](const Entity& e){return e.ref==invite.owned;});
            require(entity!=snapshot->world.end(),"bot owned entity disappeared");
            Input input;input.life=entity->life;input.yaw=entity->angles[1];
            auto* player=UObject::TryCast<UPlayerPawn>(replica.actor(invite.owned));
            if(RuneCharacters::Selected()==openrune::CharacterChoice{11,1}) {
                auto* head=static_cast<UObject**>(player->GetProperty("SkelGroupSkins"))[5];
                if(head && head->Name=="ragnarJarl_head")selectedSkinSeen=true;
            } else selectedSkinSeen=true;
            std::optional<ActionKind> action;
            if(peer.status() && peer.status()->phase==MatchPhase::Playing && now>=lastAction+1000) {
                if(entity->health<=0)action=ActionKind::Respawn;
                else if(commandline->HasArg("","--rune-net-bot-stress")) {
                    const auto phase=(now-start)/1000+ownedSlot;
                    action=phase%5==0?ActionKind::Jump:phase%5==1?ActionKind::Kick:phase%5==2?ActionKind::Attack:phase%5==3?ActionKind::RaiseShield:ActionKind::LowerShield;
                }
                if(commandline->HasArg("","--rune-net-test-headball-input") && entity->health>0) {
                    if(!peer.status()->headBallStarted)for(auto* a:game.Level->Actors)
                        if(a && a->IsA("HBGamePump") && peer.status()->scores.size()>=ownedSlot && a->GetByte("TeamNumber")==peer.status()->scores[ownedSlot-1].team &&
                           !a->GetBool("bAdminOverride") && length(a->Location()-player->Location())<110) {
                            input.yaw=uint16_t(Rotator::FromVector(a->Location()-player->Location()).Yaw);action=ActionKind::Use;break;
                        }
                    if(peer.status()->headBallStarted && !hbStartedAt)hbStartedAt=now;
                    if(ownedSlot==1 && hbStartedAt && now>=hbStartedAt+3000 && !thrown && player->Weapon() && player->Weapon()->IsA("Head")) {
                        action=ActionKind::Throw;thrown=true;
                    }
                }
            }
            if(commandline->HasArg("","--rune-net-bot-stress") && entity->health>0 && peer.status() && peer.status()->phase==MatchPhase::Playing) {
                auto phase=(now-start)/1500+ownedSlot;input.yaw=uint16_t(phase*8192);input.forward=phase%3?700:-700;input.strafe=phase%2?300:-300;
                input.held=phase%5==3?2:0;
            }
            if(motionOnly && entity->health>0 && peer.status() && peer.status()->phase==MatchPhase::Playing) {
                input.yaw=0;input.strafe=((now-joinedAt)/1500)%2?300:-300;
            }
            if(!scripted && peer.status()) {
                // Apply the authenticated snapshot to presentation/collision only.
                // The brain submits the same input/actions as a human client.
                replica.present(1.0);
                auto decision=brain.think(game,replica,snapshot->world,*peer.status(),invite.owned,now);
                input=decision.input;action=decision.action;
            }
            require(peer.inputs.capture(input,now).has_value(),"bot input queue");lastInput=now;botActivity=true;
            if(action){require(peer.inputs.act(*action,now).has_value(),"bot action queue");lastAction=now;}
            for(auto c:peer.drainCombat())require(replica.queueCombat(c,now),"bot combat queue");
            auto applied=replica.flushCombat(now);require(applied!=RuneNetworkReplica::CombatQueue::Fault,"bot combat dependencies");
            if(applied==RuneNetworkReplica::CombatQueue::Resync)peer.world.resync();
            replica.drainResolvedCombat();peer.inputs.drainResults();peer.inputs.drainUnconfirmed();
        }
        if(peer.status() && (!prior || *prior!=*peer.status())) {
            prior=*peer.status();std::cout<<"OPEN_RUNE_BOT_STATUS epoch="<<peer.mapEpoch()<<" phase="<<int(prior->phase)<<" winner="<<prior->winner<<" team="<<int(prior->winningTeam);
            for(const auto& s:prior->scores)std::cout<<" p"<<s.player<<"="<<s.frags<<","<<s.deaths;
            std::cout<<std::endl;
        }
        socket.send(session,remote);
        if(!idleWait)networkPause(2);
        else {
            // Bots do not need human-grade latency. Waking on every datagram measured
            // ~590 loops/s versus ~420 for the fixed 2 ms poll, so use timed waits
            // only: the old 2 ms floor while work is queued or input was just
            // captured, otherwise sleep to the next ACK/retransmit/input deadline
            // (<=10 ms) and let arriving datagrams batch in the socket buffer.
            const auto after=nowMs();
            uint64_t waitMs=botActivity?2:std::clamp<uint64_t>(session.idleMs(after,10),2,10);
            // Joining receives a reliable bootstrap burst; keep the 2 ms drain until joined
            // (a 10 ms wait measured transmit timeouts there, none afterwards).
            if(joined)waitMs=std::min<uint64_t>(waitMs,std::max<uint64_t>(2,lastInput+20>after?lastInput+20-after:0));
            else waitMs=2;
            networkPause(unsigned(waitMs));
        }
    }
    require(worlds>0,"bot received no world");
    peer.close(CloseReason::Leave);
    auto until=nowMs()+300;
    while(nowMs()<until && !session.failed()) {
        socket.receive([&](const sockaddr_in& source,std::span<const uint8_t> bytes){if(sameAddress(source,remote))session.receiveDatagram(bytes);});
        session.step(nowMs());peer.step();socket.send(session,remote);networkPause(2);
    }
    require(selectedSkinSeen,"selected skin never arrived");
    std::cout<<"OPEN_RUNE_BOT_AI decisions="<<brain.decisions<<" punishes="<<brain.punishes<<" leg_counters="<<brain.legCounters<<" throws="<<brain.throws<<" spacing="<<brain.spacing<<" guard_punishes="<<brain.guardPunishes<<" guard_escapes="<<brain.guardEscapes<<" third_party="<<brain.thirdParty<<" wounded_targets="<<brain.woundedTargets<<" target_overrides="<<brain.targetOverrides<<" early_swings="<<brain.earlySwings<<" swing_holds="<<brain.swingHolds<<" attacks="<<brain.attacks<<" defenses="<<brain.defenses<<" specials="<<brain.specials<<" objectives="<<brain.objectives<<" routes="<<brain.routes<<" bashes="<<brain.bashes<<" crossed_blocks="<<brain.crossBlocks<<" leaps="<<brain.leaps<<" charges="<<brain.charges<<" powers="<<brain.powers<<" max_think_us="<<brain.maxThinkUs<<std::endl;
    std::cout<<"OPEN_RUNE_BOT_OK loops="<<loops<<" worlds="<<worlds<<" max_world_gap_ms="<<maxWorldGap<<" gap_buckets=";for(auto count:worldGaps)std::cout<<count<<",";
    std::cout<<" active_updates="<<activeWorlds<<" active_ms="<<activeMs<<" active_max_gap_ms="<<activeMaxGap<<" active_gap_buckets=";for(auto count:activeGaps)std::cout<<count<<",";std::cout<<std::endl;
}
