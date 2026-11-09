#include "Precomp.h"
#include "RuneDesktopSession.h"
#include "Engine.h"
#include "RuneBloodlust.h"
#include "Utils/File.h"
#include "Utils/UTF16.h"
#include "Utils/Logger.h"
#include <fstream>
#include <chrono>
#include <sstream>
#include <sddl.h>
namespace {
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
std::wstring quoted(const std::wstring& s){std::wstring out=L"\"";size_t slashes=0;for(auto c:s){if(c==L'\\'){++slashes;continue;}out.append(slashes*(c==L'"'?2:1),L'\\');slashes=0;if(c==L'"')out+=L'\\';out+=c;}out.append(slashes*2,L'\\');return out+L'"';}
void writeJson(const fs::path& path,const JsonValue& value){auto temp=path;temp+=L".tmp";std::ofstream f(temp,std::ios::binary);f<<value.to_json();f.close();check(bool(f),"Cannot save discovery command");check(MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Cannot publish discovery command");}
JsonValue readJson(const fs::path& p){
 // The runtime publishes by rename. Windows readers must allow deletion of the
 // old directory entry while holding it open, or Node can fail with EPERM.
 HANDLE file=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND)return JsonValue::object();throw std::runtime_error("Cannot read discovery response");}
 LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart<0||size.QuadPart>=512*1024){CloseHandle(file);throw std::runtime_error("Discovery response exceeds size limit");}
 std::string data((size_t)size.QuadPart,'\0');DWORD bytes=0;BOOL ok=ReadFile(file,data.data(),(DWORD)data.size(),&bytes,nullptr);CloseHandle(file);
 check(ok&&bytes==data.size(),"Cannot read complete discovery response");return JsonValue::parse(data);
}
struct Child {
    HANDLE process=nullptr,job=nullptr;
    ~Child(){stop();}
    bool running()const{return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
    void stop(){if(job){TerminateJobObject(job,0);CloseHandle(job);job=nullptr;}if(process){CloseHandle(process);process=nullptr;}}
    void launch(const fs::path& exe,const std::vector<std::string>& args,const fs::path& root){
        stop();check(fs::is_regular_file(exe),"Missing runtime: "+exe.string());fs::create_directories(root/"config/SurrealEngine");
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE log=CreateFileW((root/"runtime.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);check(log!=INVALID_HANDLE_VALUE,"Cannot create session log");
        job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limit,sizeof(limit))){CloseHandle(log);stop();throw std::runtime_error("Cannot contain session processes");}
        std::wstring command=quoted(exe.wstring());for(const auto& a:args)command+=L" "+quoted(to_utf16(a));
        std::vector<wchar_t> env;auto block=GetEnvironmentStringsW();for(auto p=block;*p;p+=wcslen(p)+1)if(_wcsnicmp(p,L"SURREALENGINE_CONFIG_DIR=",25)!=0)env.insert(env.end(),p,p+wcslen(p)+1);FreeEnvironmentStringsW(block);
        auto entry=L"SURREALENGINE_CONFIG_DIR="+(root/"config").wstring();env.insert(env.end(),entry.begin(),entry.end());env.push_back(0);env.push_back(0);
        STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=log;si.hStdError=log;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);PROCESS_INFORMATION pi{};
        BOOL made=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT,env.data(),root.c_str(),&si,&pi);CloseHandle(log);
        if(!made){stop();throw std::runtime_error("Could not start "+exe.filename().string());}
        process=pi.hProcess;
        if(!AssignProcessToJobObject(job,process)){TerminateProcess(process,1);CloseHandle(pi.hThread);stop();throw std::runtime_error("Cannot track child process lifetime");}
        ResumeThread(pi.hThread);CloseHandle(pi.hThread);
    }
};
fs::path sessionRoot(){
 static unsigned count=0;auto parent=Directory::localAppData()/"SurrealEngine/OpenRuneSessions";fs::create_directories(parent);
 auto path=parent/(std::to_string(GetCurrentProcessId())+"-"+std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count())+"-"+std::to_string(++count));
 HANDLE token=nullptr;check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)!=0,"Cannot identify session owner");
 DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<uint8_t> info(bytes);
 BOOL queried=GetTokenInformation(token,TokenUser,info.data(),bytes,&bytes);CloseHandle(token);check(queried!=0,"Cannot read session owner");
 LPWSTR owner=nullptr;check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid,&owner)!=0,"Cannot encode session owner");
 std::wstring acl=L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;"+std::wstring(owner)+L")";LocalFree(owner);
 PSECURITY_DESCRIPTOR descriptor=nullptr;check(ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)!=0,"Cannot protect session files");
 SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};BOOL created=CreateDirectoryW(path.c_str(),&attributes);LocalFree(descriptor);
 check(created!=0,"Cannot create a new private session directory");return path;
}
fs::path runtime(){auto p=fs::path(OS::executable_path())/"discovery/runtime.cjs";check(fs::is_regular_file(p),"Discovery runtime is missing. Run Windows/Prepare-Discovery.ps1.");return p;}
fs::path node(){auto bundled=fs::path(OS::executable_path())/"discovery/node.exe";if(fs::is_regular_file(bundled))return bundled;wchar_t folder[32768]{};GetEnvironmentVariableW(L"ProgramFiles",folder,32768);auto installed=fs::path(folder)/"nodejs/node.exe";check(fs::is_regular_file(installed),"Node.js is required for Internet discovery. Run Windows/Prepare-Discovery.ps1.");return installed;}
}
bool RuneDesktopHost::Supports(const std::string& map,const std::string& mode,int teams,int size){
 if(map.empty()||map.size()>48||!std::all_of(map.begin(),map.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_';}))return false;
 if(mode=="dm")return map.starts_with("DM-");
 if(mode=="tdm"){int max=map=="DM-Team_chasm"||map=="DM-Team_ships"?2:map=="DM-TEAM_MadKing"?3:4;return map.starts_with("DM-")&&teams<=max;}
 if(mode=="headball")return map.starts_with("HB-")&&(teams==2||map=="HB-Aqueduct"||map=="HB-DeCapital"||map=="HB-Montezuma"||map=="HB-Montezuma2");
 int max=map=="AR-Champions"||map=="AR-SmeltingPot"?4:map=="AR-AEternal"||map=="AR-BladePit"?3:map=="AR-Cathedral"||map=="AR-TeamVent"||map=="AR-Widowmaker"?2:1;
 return mode=="arena"&&map.starts_with("AR-")&&size<=max;
}
std::vector<std::string> RuneDesktopHost::Available(const Engine& game)const {std::vector<std::string> out;for(auto& p:fs::directory_iterator(fs::path(game.LaunchInfo.gameRootFolder)/"Maps"))if(p.is_regular_file()&&p.path().extension()==".run"&&Supports(p.path().stem().string(),mode,teams,arenaSize))out.push_back(p.path().stem().string());std::sort(out.begin(),out.end());return out;}
void RuneDesktopHost::Normalize(const Engine& game){auto all=Available(game);std::erase_if(maps,[&](auto& m){return std::find(all.begin(),all.end(),m)==all.end();});if(maps.empty()&&!all.empty()){std::string preferred=mode=="arena"?"AR-Champions":mode=="headball"?"HB-Agora":mode=="tdm"?"DM-Team_ships":"DM-Hildir";maps.push_back(std::find(all.begin(),all.end(),preferred)!=all.end()?preferred:all.front());}players=std::clamp(std::max(players,mode=="arena"?arenaSize*2:mode=="dm"?2:teams),2,16);bots=std::clamp(bots,0,players-1);}
struct RuneDesktopSession::Impl {
 struct Bot {Child client,discovery;fs::path root;Clock::time_point began;int slot=0;bool starting=true;};
 std::vector<std::unique_ptr<Bot>> bots;int queuedBots=0;std::string gameRoot;fs::path exe;
 Child server,hostRuntime,browser;fs::path hostRoot,browseRoot;RuneDesktopHost config;std::string invite;bool preparing=false,joinPending=false;int command=0;Clock::time_point began{},lastPoll{};
 void send(const std::string& action,const std::string& key=""){if(!browser.running())throw std::runtime_error("Discovery is not running");auto c=JsonValue::object();c["id"]=JsonValue::number(++command);c["action"]=JsonValue::string(action);if(!key.empty())c["key"]=JsonValue::string(key);writeJson(browseRoot/"command.json",c);}
 void ensureBrowser(){if(browser.running())return;browseRoot=sessionRoot()/"browser";fs::create_directories(browseRoot);auto c=JsonValue::object();c["control"]=JsonValue::string(browseRoot.string());writeJson(browseRoot/"config.json",c);browser.launch(node(),{runtime().string(),(browseRoot/"config.json").string()},browseRoot);}
};
RuneDesktopSession::RuneDesktopSession():impl(std::make_unique<Impl>()){}
RuneDesktopSession::~RuneDesktopSession(){Leave();StopHost();}
bool RuneDesktopSession::Hosting()const{return impl->server.running();}
bool RuneDesktopSession::Busy()const{return impl->preparing||impl->joinPending;}
void RuneDesktopSession::Host(Engine& game,RuneDesktopHost c){check(!Hosting(),"Stop your current host first.");c.Normalize(game);check(!c.maps.empty(),"No compatible maps are installed.");check(c.name.size()>=1&&c.name.size()<=48&&std::all_of(c.name.begin(),c.name.end(),[](unsigned char x){return x>=32&&x<127;}),"Use a name of 1–48 printable characters.");check(c.score>=0&&c.score<=10000&&c.minutes>=0&&c.minutes<=1440&&(c.score||c.minutes),"Enable a score or time limit.");check(c.maps.size()<=32,"Choose up to 32 maps.");
 if(c.publicGame){runtime();node();}
 impl->config=c;impl->queuedBots=c.bots;impl->gameRoot=game.LaunchInfo.gameRootFolder;impl->hostRoot=sessionRoot()/"host";fs::create_directories(impl->hostRoot/"saves");InvitationDirectory=impl->hostRoot/"invitations";std::string maps;for(auto& m:c.maps){if(!maps.empty())maps+=",";maps+=m;}
 std::vector<std::string> args={"--direct","--save-directory="+(impl->hostRoot/"saves").string(),"--rune-server="+InvitationDirectory.string(),"--rune-net-address="+(c.publicGame?"127.0.0.1":c.address),"--rune-net-mode="+c.mode,"--rune-net-players="+std::to_string(c.players),"--rune-net-teams="+std::to_string(c.teams),"--rune-net-arena-team-size="+std::to_string(c.arenaSize),"--rune-net-frag-limit="+std::to_string(c.score),"--rune-net-round-limit="+std::to_string(c.score),"--rune-net-time-limit="+std::to_string(c.minutes*60),"--rune-net-maps="+maps};args.push_back("--rune-net-discovery");if(c.mode=="headball"&&c.limbs)args.push_back("--rune-net-limbs-score");args.push_back((c.laws&RuneBattleLaws::Normal)?"--rune-normal-bloodlust-moves=1":"--rune-normal-bloodlust-moves=0");args.push_back("--rune-battle-laws="+std::to_string(c.laws));args.push_back("--rune-physics="+std::to_string(c.physics));args.push_back(game.LaunchInfo.gameRootFolder);
 wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);impl->exe=exe;impl->server.launch(exe,args,impl->hostRoot);impl->preparing=true;impl->began=Clock::now();HostKey.clear();Message="Preparing your match…";
}
int RuneDesktopSession::BotCount()const{return (int)impl->bots.size()+impl->queuedBots;}
void RuneDesktopSession::AddBot(){
 check(Hosting()&&!impl->preparing,"Wait until your host is ready.");
 check(BotCount()<impl->config.players-1,"The player limit includes you and the bots.");
 ++impl->queuedBots;Message="Adding a bot…";
}
void RuneDesktopSession::RemoveBot(){
 check(Hosting(),"You are not hosting.");
 if(impl->queuedBots)--impl->queuedBots;
 else if(!impl->bots.empty())impl->bots.pop_back();
 else throw std::runtime_error("No bots to remove.");
 Message="Bot removed. Its player slot may take a moment to become available.";
}
void RuneDesktopSession::Browse(){check(!impl->joinPending,"Cancel the current connection first.");impl->ensureBrowser();impl->send("browse");Rows=JsonValue::array();Message="Searching for games…";}
void RuneDesktopSession::Join(const std::string& key){check(key.size()==64&&std::all_of(key.begin(),key.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),"Enter a 64-character host identity.");impl->ensureBrowser();impl->send("join",key);LastKey=key;impl->joinPending=true;impl->began=Clock::now();Message="Connecting to host…";}
void RuneDesktopSession::Cancel(){if(impl->browser.running())impl->send("cancel");impl->joinPending=false;Message="Cancelled.";}
void RuneDesktopSession::Leave(){
 // Keep the discovery process and its peer identity alive. The host reserves
 // a disconnected player's slot for 45 seconds so that identity can rejoin.
 if(impl->browser.running())try{impl->send("leave");}catch(...){}
 impl->joinPending=false;impl->invite.clear();Rows=JsonValue::array();Message="Disconnected.";
}
void RuneDesktopSession::StopHost(){impl->queuedBots=0;impl->bots.clear();impl->hostRuntime.stop();impl->server.stop();impl->preparing=false;HostKey.clear();InvitationDirectory.clear();Message="Hosting stopped.";}
std::string RuneDesktopSession::TakeInvite(){std::string result;result.swap(impl->invite);return result;}
void RuneDesktopSession::Tick(){if(Clock::now()-impl->lastPoll<std::chrono::milliseconds(100))return;impl->lastPoll=Clock::now();try {
 if(impl->preparing){check(impl->server.running(),"Host failed to start. See the session runtime.log.");check(Clock::now()-impl->began<std::chrono::seconds(45),"Host publication timed out. Retry or use an invitation.");
  if(fs::exists(InvitationDirectory/("player"+std::to_string(impl->config.players)+".rune-invite"))){
   if(!impl->config.publicGame){impl->invite=(InvitationDirectory/"player1.rune-invite").string();impl->preparing=false;Message="Invitation host ready.";}
   else if(!impl->hostRuntime.process&&fs::exists(InvitationDirectory/"discovery-status.json")){auto dir=impl->hostRoot/"discovery";fs::create_directories(dir);auto c=JsonValue::object();c["control"]=JsonValue::string(dir.string());c["directory"]=JsonValue::string(InvitationDirectory.string());c["firstSlot"]=JsonValue::number(2);c["name"]=JsonValue::string(impl->config.name);writeJson(dir/"config.json",c);impl->hostRuntime.launch(node(),{runtime().string(),(dir/"config.json").string()},dir);}
   else if(impl->hostRuntime.process){check(impl->hostRuntime.running(),"Discovery runtime stopped.");auto state=readJson(impl->hostRoot/"discovery/state.json");auto phase=state["phase"].to_string();if(phase=="error")throw std::runtime_error(state["message"].to_string());if(phase=="hosting"){HostKey=state["key"].to_string();impl->invite=(InvitationDirectory/"player1.rune-invite").string();impl->preparing=false;Message="Listed in Find games.";}}
  }
 }
 if(!impl->preparing&&Hosting()){
  // Join through the same admission path as human players. Public bots receive
  // leased slots so simultaneous Internet joins cannot steal their invitations.
  bool pending=false;
  for(auto it=impl->bots.begin();it!=impl->bots.end();){auto& b=**it;bool failed=false;
   try{if(b.starting){pending=true;
    if(Clock::now()-b.began>std::chrono::seconds(40))failed=true;
    else if(impl->config.publicGame){
     auto state=readJson(b.root/"discovery/state.json");auto phase=state["phase"].to_string();
     if(phase=="error"||!b.discovery.running())failed=true;
     else if(phase=="joined"){
      b.slot=state["slot"].to_int();
      b.client.launch(impl->exe,{"--direct","--no-desktop-menu","--rune-net-bot","--rune-net-seconds=0","--rune-join="+(b.root/"discovery/joined.rune-invite").string(),"--save-directory="+(b.root/"saves").string(),impl->gameRoot},b.root/"client");b.starting=false;
     }
    }
   }else if(!b.client.running()||(impl->config.publicGame&&!b.discovery.running()))failed=true;
   }catch(const std::exception& e){failed=true;LogMessage("DESKTOP_BOT_ERROR "+std::string(e.what()));}
   if(failed){it=impl->bots.erase(it);Message="A bot could not join or disconnected. Check capacity and try Add bot again.";}
   else ++it;
  }
  if(impl->queuedBots&&!pending){
   auto status=readJson(InvitationDirectory/"discovery-status.json");
   std::vector<int> used{1};for(auto& slot:status["occupied"].items())used.push_back(slot.to_int());for(auto& b:impl->bots)used.push_back(b->slot);
   int slot=impl->config.players;while(slot>1&&std::find(used.begin(),used.end(),slot)!=used.end())--slot;
   if(slot<=1){impl->queuedBots=0;Message="Match is full. Increase the player limit before hosting, or wait for a free slot.";}
   else{
    --impl->queuedBots; // Consume once even if a process cannot start.
    auto b=std::make_unique<Impl::Bot>();b->root=sessionRoot()/"bot";b->began=Clock::now();fs::create_directories(b->root/"saves");
    if(impl->config.publicGame){
     auto dir=b->root/"discovery";fs::create_directories(dir);auto c=JsonValue::object();c["control"]=JsonValue::string(dir.string());writeJson(dir/"config.json",c);
     b->discovery.launch(node(),{runtime().string(),(dir/"config.json").string()},dir);
     c=JsonValue::object();c["id"]=JsonValue::number(1);c["action"]=JsonValue::string("join");c["key"]=JsonValue::string(HostKey);writeJson(dir/"command.json",c);
    }else{
     b->slot=slot;
     // Private invitations advertise the LAN address, also reachable locally.
     b->client.launch(impl->exe,{"--direct","--no-desktop-menu","--rune-net-bot","--rune-net-seconds=0","--rune-join="+(InvitationDirectory/("player"+std::to_string(slot)+".rune-invite")).string(),"--save-directory="+(b->root/"saves").string(),impl->gameRoot},b->root/"client");b->starting=false;
    }
    impl->bots.push_back(std::move(b));
   }
  }
 }else if(!impl->preparing&&!impl->bots.empty()){impl->bots.clear();impl->queuedBots=0;}
 if(impl->browser.process){check(impl->browser.running(),"Discovery runtime stopped. Refresh to retry.");auto state=readJson(impl->browseRoot/"state.json");if(state["rows"].type()==JsonType::array)Rows=state["rows"];auto phase=state["phase"].to_string();if(!phase.empty())Message=state["message"].to_string();if(phase=="joined"&&impl->joinPending&&state["joinId"].to_int()==impl->command){impl->invite=(impl->browseRoot/"joined.rune-invite").string();impl->joinPending=false;}
  if(phase=="error"||phase=="lost")impl->joinPending=false;
  if(impl->joinPending&&Clock::now()-impl->began>std::chrono::seconds(35)){Cancel();Message="Connection timed out. The host may be unreachable.";}
 }
 if(impl->hostRuntime.process&&!impl->hostRuntime.running()&&!impl->preparing)Message="Host discovery stopped. Stop hosting and try again.";
 }catch(const std::exception& e){std::string error=e.what();if(impl->preparing)StopHost();impl->joinPending=false;Message=std::move(error);LogMessage("DESKTOP_DISCOVERY_ERROR "+Message);}}
