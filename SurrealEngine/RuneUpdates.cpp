#include "Precomp.h"
#include "RuneUpdates.h"
#include "RuneReleaseVersion.h"
#include "OpenRuneVersion.h"
#include "Utils/JsonValue.h"
#include "Utils/File.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#ifdef OPENRUNE_ENABLE_NETWORK
#include <openrune/net/Protocol.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
extern char** environ;
#endif
namespace RuneUpdates {
namespace {
constexpr size_t Limit=262144;
std::string Base(){return std::string("https://github.com/")+OpenRuneBuild::Repository+"/releases";}
std::string Plain(std::string text,size_t max) {
    if(text.size()>max)text.resize(max);
    for(char& c:text)if((unsigned char)c<32 && c!='\n' && c!='\t')c=' ';
    return text;
}
struct Shared {std::mutex mutex;Result result;bool automaticStarted=false;std::chrono::steady_clock::time_point checked{};};
std::shared_ptr<Shared> State(){static auto s=std::make_shared<Shared>();return s;}
std::filesystem::path Preference(){return Directory::localAppData()/"OpenRune"/"update-checks.txt";}
bool Allowed(const std::string& url) {
    if(url==Base())return true;
    auto prefix=Base()+"/tag/";
    if(!url.starts_with(prefix))return false;
    return Version::Parse(url.substr(prefix.size())).has_value();
}
}
std::string Identity(){
    std::string text=std::string("Open Rune ")+OpenRuneBuild::Version+" (build "+std::to_string(OpenRuneBuild::Build)+")";
#ifdef OPENRUNE_ENABLE_NETWORK
    text+=" | Network "+std::to_string(openrune::net::ProtocolVersion)+" | Rules "+std::to_string(OpenRuneBuild::GameplayRulesRevision);
#endif
    return text;
}
Result Inspect(int status,const std::string& body) {
    Result r;r.url=Base();
    if(status==404){r.message="No public stable release is available. The repository may still be private.";return r;}
    if(status==403||status==429){r.message="GitHub has limited update checks. Try again later.";return r;}
    if(status!=200){r.message="Could not check releases. Check your connection and try again.";return r;}
    try {
        if(body.size()>Limit)throw std::runtime_error("oversized release");
        const auto data=JsonValue::parse(body);
        if(!data.is_object()||!data["draft"].is_boolean()||!data["prerelease"].is_boolean()||data["draft"].to_boolean()||data["prerelease"].to_boolean())throw std::runtime_error("not a published stable release");
        if(data["tag_name"].type()!=JsonType::string||data["html_url"].type()!=JsonType::string||(data["body"].type()!=JsonType::string&&!data["body"].is_null()))throw std::runtime_error("missing release data");
        auto tag=data["tag_name"].to_string();auto remote=Version::Parse(tag),local=Version::Parse(OpenRuneBuild::Version);
        if(!remote||!local||!remote->pre.empty()||data["html_url"].to_string()!=Base()+"/tag/"+tag)throw std::runtime_error("invalid release identity");
        r.version=tag;r.url=data["html_url"].to_string();r.notes=Plain(data["body"].to_string(),6000);
        r.available=remote->Compare(*local)>0;
        r.message=r.available?"Update available: "+tag:"No newer stable release. Installed: "+std::string(OpenRuneBuild::Version);
    }catch(const std::exception&){r=Result{};r.message="GitHub returned an invalid release response. Nothing was downloaded.";r.url=Base();}
    return r;
}
Result Fetch() {
    try {
        std::string body;int status=0;
#ifdef _WIN32
        struct Handle {HINTERNET h;~Handle(){if(h)WinHttpCloseHandle(h);}operator HINTERNET()const{return h;}};
        Handle session{WinHttpOpen(L"OpenRune-ReleaseChecker/1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
        if(!session.h)throw std::runtime_error("session");
        WinHttpSetTimeouts(session,4000,4000,4000,4000);
        Handle connection{WinHttpConnect(session,L"api.github.com",INTERNET_DEFAULT_HTTPS_PORT,0)};
        if(!connection.h)throw std::runtime_error("connect");
        auto path=std::string("/repos/")+OpenRuneBuild::Repository+"/releases/latest";std::wstring wide(path.begin(),path.end());
        Handle request{WinHttpOpenRequest(connection,L"GET",wide.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};
        if(!request.h)throw std::runtime_error("request");
        DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects));
        if(!WinHttpSendRequest(request,L"Accept: application/vnd.github+json\r\n",DWORD(-1),WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,nullptr))throw std::runtime_error("https");
        DWORD code=0,length=sizeof(code);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&length,WINHTTP_NO_HEADER_INDEX))throw std::runtime_error("status");status=int(code);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
        for(;;){char bytes[4096];DWORD read=0;if(std::chrono::steady_clock::now()>deadline||!WinHttpReadData(request,bytes,sizeof(bytes),&read))throw std::runtime_error("read");if(!read)break;if(body.size()+read>Limit)throw std::runtime_error("size");body.append(bytes,read);}
#elif !defined(__ANDROID__)
        // Fixed executable and argv, never a shell. curl retains normal certificate validation.
        int pipes[2];if(pipe(pipes))throw std::runtime_error("pipe");
        posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,pipes[1],STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions,pipes[0]);posix_spawn_file_actions_addclose(&actions,pipes[1]);
        auto url=std::string("https://api.github.com/repos/")+OpenRuneBuild::Repository+"/releases/latest";
        const char* args[]={"/usr/bin/curl","--silent","--show-error","--connect-timeout","5","--max-time","12","--proto","=https","--max-filesize","262144","--user-agent","OpenRune-ReleaseChecker/1","--header","Accept: application/vnd.github+json","--write-out","\n%{http_code}",url.c_str(),nullptr};
        pid_t child;int error=posix_spawn(&child,args[0],&actions,nullptr,const_cast<char**>(args),environ);posix_spawn_file_actions_destroy(&actions);close(pipes[1]);
        if(error){close(pipes[0]);throw std::runtime_error("curl unavailable");}
        bool overflow=false;for(;;){char bytes[4096];auto n=read(pipes[0],bytes,sizeof(bytes));if(n<0&&errno==EINTR)continue;if(n<=0)break;if(body.size()+size_t(n)>Limit+4){overflow=true;kill(child,SIGTERM);break;}body.append(bytes,size_t(n));}close(pipes[0]);
        int exit=0;while(waitpid(child,&exit,0)<0){if(errno!=EINTR)throw std::runtime_error("wait");}
        if(overflow||!WIFEXITED(exit)||WEXITSTATUS(exit)!=0)throw std::runtime_error("https failed");
        auto end=body.rfind('\n');if(end==body.npos||body.size()-end!=4)throw std::runtime_error("status");status=std::stoi(body.substr(end+1));body.resize(end);
#else
        throw std::runtime_error("Use the Android release checker");
#endif
        return Inspect(status,body);
    }catch(const std::exception&){Result r;r.message="Could not check releases. Check your connection and try again.";r.url=Base();return r;}
}
Result Current(){auto s=State();std::lock_guard lock(s->mutex);return s->result;}
bool Automatic(){std::ifstream in(Preference());std::string choice;return !(in>>choice)||choice!="off";}
void SetAutomatic(bool enabled){auto file=Preference();std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);std::ofstream out(file);out<<(enabled?"on":"off");}
void Check(bool manual){
    if(!manual&&!Automatic())return;
    auto s=State();std::lock_guard lock(s->mutex);const auto now=std::chrono::steady_clock::now();
    if(s->result.busy||(!manual&&s->automaticStarted)||(s->checked.time_since_epoch().count()&&now-s->checked<std::chrono::seconds(60)))return;
    s->automaticStarted=true;s->checked=now;s->result.busy=true;s->result.message="Checking published releases…";
    std::thread([s]{auto r=Fetch();std::lock_guard done(s->mutex);s->result=std::move(r);}).detach();
}
bool OpenPage(const std::string& url){
    if(!Allowed(url))return false;
#ifdef _WIN32
    std::wstring wide(url.begin(),url.end());return reinterpret_cast<intptr_t>(ShellExecuteW(nullptr,L"open",wide.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
#elif !defined(__ANDROID__)
#ifdef __APPLE__
    const char* program="/usr/bin/open";
#else
    const char* program="/usr/bin/xdg-open";
#endif
    char* args[]={const_cast<char*>(program),const_cast<char*>(url.c_str()),nullptr};pid_t child;
    if(posix_spawn(&child,program,nullptr,nullptr,args,environ))return false;
    std::thread([child]{int status;while(waitpid(child,&status,0)<0&&errno==EINTR){}}).detach();return true;
#else
    return false;
#endif
}
int Probe(){
    int checks=0;auto check=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(std::string("Update check failed: ")+name);};
    for(auto bad:{"","1","1.2","1.2.3.4","01.2.3","1.02.3","1.2.03","1.2.3-","1.2.3-a..b","1.2.3-01","1.2.3+","1.2.3+a..b","1.2.3/evil","999999999999999.0.0"})check(!Version::Parse(bad),"invalid version");
    const char* ordered[]={"1.0.0-alpha","1.0.0-alpha.1","1.0.0-alpha.beta","1.0.0-beta","1.0.0-beta.2","1.0.0-beta.11","1.0.0-rc.1","1.0.0","1.0.1","1.1.0","2.0.0"};
    for(size_t i=1;i<std::size(ordered);++i)check(Version::Parse(ordered[i-1])->Compare(*Version::Parse(ordered[i]))<0,"version precedence");
    check(Version::Parse("v1.2.3+a")->Compare(*Version::Parse("1.2.3+b"))==0,"build metadata ignored");
    auto body=[](std::string tag){return std::string("{\"tag_name\":\"")+tag+"\",\"html_url\":\""+Base()+"/tag/"+tag+"\",\"draft\":false,\"prerelease\":false,\"body\":\"Release notes\\nSecond line\"}";};
    check(Inspect(200,body("v999.0.0")).available,"new release");check(!Inspect(200,body("v0.0.1")).available,"no downgrade");
    auto draft=JsonValue::parse(body("v999.0.0"));draft["draft"]=JsonValue::boolean(true);check(!Inspect(200,draft.to_json()).available,"draft ignored");draft["draft"]=JsonValue::boolean(false);draft["prerelease"]=JsonValue::boolean(true);check(!Inspect(200,draft.to_json()).available,"preview ignored");
    auto unsafe=JsonValue::parse(body("v999.0.0"));unsafe["html_url"]=JsonValue::string("https://evil.example/update");check(!Inspect(200,unsafe.to_json()).available,"untrusted link");
    for(auto malformed:{"null","[]","{}","not json"})check(!Inspect(200,malformed).available,"invalid response");
    check(!Inspect(200,std::string(Limit+1,' ')).available,"response bound");check(!Inspect(404,"").available,"private/no release");check(!Inspect(429,"").available,"rate limit");check(!Inspect(500,"").available,"offline");
    check(Allowed(Base()+"/tag/v1.2.3")&&!Allowed(Base()+"/tag/v1.2.3/../../evil")&&!Allowed("file:///tmp/test"),"browser allowlist");
    std::cout<<"OPEN_RUNE_UPDATE_PROBE_OK checks="<<checks<<" "<<Identity()<<std::endl;return 0;
}
}
