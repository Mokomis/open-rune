#include "Precomp.h"
#include "RuneSetupWindow.h"
#include "GameFolder.h"
#include "RuneGameFiles.h"
#include "Utils/File.h"
#include "Utils/CommandLine.h"
#include "LauncherBanner.h"
#include <surrealwidgets/core/widget.h>
#include <surrealwidgets/core/timer.h>
#include <surrealwidgets/widgets/textlabel/textlabel.h>
#include <surrealwidgets/widgets/pushbutton/pushbutton.h>
#include <surrealwidgets/systemdialogs/open_folder_dialog.h>
#include <atomic>
#include <future>
#include <iostream>
#ifdef WIN32
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace RuneSetup {
static fs::path DataRoot(){return Directory::localAppData()/"OpenRune"/"game";}
#ifdef WIN32
static void PrepareTextures()
{
    std::error_code error;
    if (fs::is_regular_file(DataRoot() / "TexturePacks/OpenRune-release/index.tsv", error)) return;

    std::wstring module(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, module.data(), DWORD(module.size()));
    if (!length || length >= module.size()) return;
    module.resize(length);
    const auto folder = fs::path(module).parent_path();
    const auto installer = folder / L"Prepare Textures.ps1";
    if (!fs::is_regular_file(installer, error)) return;
    wchar_t system[32768];
    length = GetSystemDirectoryW(system, 32768);
    if (!length || length >= 32768) return;
    const auto powershell = fs::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
    auto args = L"\"" + powershell.wstring() + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""
        + installer.wstring() + L"\" -GameDirectory \"" + DataRoot().wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(powershell.c_str(), args.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, folder.c_str(), &startup, &process))
    {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}
#endif
static bool Buy() {
#ifdef WIN32
    return reinterpret_cast<intptr_t>(ShellExecuteW(nullptr,L"open",L"https://store.steampowered.com/app/210950/Rune_Classic/",nullptr,nullptr,SW_SHOWNORMAL))>32;
#else
    // A fixed executable/argument list, without invoking a shell.
#ifdef __APPLE__
    const char* program="/usr/bin/open";
#else
    const char* program="/usr/bin/xdg-open";
#endif
    char* argv[]={const_cast<char*>(program),const_cast<char*>(RuneGameFiles::SteamURL),nullptr};pid_t child;
    if(posix_spawn(&child,program,nullptr,nullptr,argv,environ)!=0)return false;
    int status=0;while(waitpid(child,&status,0)<0){if(errno!=EINTR)return false;}
    return WIFEXITED(status)&&WEXITSTATUS(status)==0;
#endif
}
class Window final:public Widget {
public:
    Window():Widget(nullptr,WidgetType::Window),poll(this) {
        SetWindowTitle("Open Rune · First-time setup");
        SetWindowIcon({Image::LoadResource("open-rune-mark.png")});
        SetStyleColor("background-color",Colorf::fromRgb(0x101314));
        banner=new LauncherBanner(this);
        heading=label("Set up your game files",28,0xeee8db);
        details=label("Open Rune requires the original Rune game files.\nSelect your Rune Classic folder, or get the game on Steam.",18,0xaaa9a3);
        choose=button("Use an existing Rune installation",[this]{Choose();});
        buy=button("Buy Rune Classic on Steam",[this]{if(!Buy())status->SetText("Could not open your browser. Visit store.steampowered.com/app/210950");});
        status=label("Choose the folder containing System, Maps and Textures.\nRequired files are copied into Open Rune's own data folder.\nYour original installation and saves stay unchanged.",16,0xaaa9a3);
        close=button("Exit",[this]{OnClose();});
        poll.FuncExpired=[this]{
            if(!work.valid())return;
            status->SetText("Copying game files: "+std::to_string(done.load())+" / "+std::to_string(total.load())+"\nPlease keep Open Rune open until the import finishes.");
            if(work.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
            auto error=work.get();poll.Stop();choose->SetEnabled(true);buy->SetEnabled(true);close->SetEnabled(true);
            if(!error.empty()){status->SetText(error);return;}
            auto imported=GameFolderSelection::ExamineFolder(DataRoot().string());
            if(imported.IsRune()){GameFolderSelection::Games={imported};result=0;DisplayWindow::ExitLoop();return;}
            status->SetText("The copied files could not be recognized. No original files were changed.");
        };
    }
    int result=-1;
private:
    TextLabel* label(const std::string& text,int size,uint32_t color){auto p=new TextLabel(this);p->SetText(text);p->SetStyleDouble("font-size",size);p->SetStyleColor("color",Colorf::fromRgb(color));return p;}
    PushButton* button(const std::string& text,std::function<void()> action){auto p=new PushButton(this);p->SetText(text);p->OnClick=std::move(action);p->SetStyleDouble("font-size",18);return p;}
    void Choose(){
        auto dialog=OpenFolderDialog::Create(this);dialog->SetTitle("Select your Rune Classic installation folder");
        if(!dialog->Show())return;
        try {
            auto source=RuneGameFiles::Root(fs::u8path(dialog->SelectedPath()));RuneGameFiles::Validate(source);
            if(!GameFolderSelection::ExamineFolder(source.string()).IsRune())throw std::runtime_error("This Rune executable version is not supported. Select Rune Classic 1.10 or 1.11, or Rune Gold 1.07.");
            choose->SetEnabled(false);buy->SetEnabled(false);close->SetEnabled(false);
            status->SetText("Preparing to copy game files…");
            work=std::async(std::launch::async,[this,source]{try{RuneGameFiles::Import(source,DataRoot(),[this](size_t a,size_t b){done=a;total=b;});return std::string();}catch(const std::exception& e){return std::string(e.what());}});
            poll.Start(100);
        }catch(const std::exception& e){status->SetText(e.what());}
    }
    void OnClose()override{if(work.valid())return;DisplayWindow::ExitLoop();}
    void OnGeometryChanged()override{
        double width=GetWidth();banner->SetFrameGeometry(24,8,width-48,100);
        heading->SetFrameGeometry(40,126,width-80,42);details->SetFrameGeometry(40,180,width-80,64);
        choose->SetFrameGeometry(40,268,width-80,58);buy->SetFrameGeometry(40,342,width-80,58);
        status->SetFrameGeometry(40,424,width-80,112);close->SetFrameGeometry(width-170,GetHeight()-64,130,42);
    }
    LauncherBanner* banner;TextLabel *heading,*details,*status;PushButton *choose,*buy,*close;
    Timer poll;std::future<std::string> work;std::atomic<size_t> done{0},total{0};
};
int SelectGame(){
    GameFolderSelection::UpdateList();
    // Explicit developer/legacy launches keep their selected installation. Normal
    // launches use only our verified private copy, never a discovered Steam root.
    if(!commandline->GetItems().empty())for(size_t i=0;i<GameFolderSelection::Games.size();++i)if(GameFolderSelection::Games[i].IsRune())return (int)i;
    try {
        RuneGameFiles::Validate(DataRoot());auto game=GameFolderSelection::ExamineFolder(DataRoot().string());
        if(game.IsRune()){
#ifdef WIN32
            PrepareTextures();
#endif
            GameFolderSelection::Games={game};return 0;
        }
    }catch(const std::exception&){}
    auto screen=Widget::GetScreenSize();auto window=std::make_unique<Window>();
    window->SetFrameGeometry((screen.width-800)/2,(screen.height-620)/2,800,620);window->Show();
    std::cout<<"RUNE_SETUP: Waiting for original game files.\n"<<std::flush;
    DisplayWindow::RunLoop();
#ifdef WIN32
    if (window->result >= 0) PrepareTextures();
#endif
    return window->result;
}
}
