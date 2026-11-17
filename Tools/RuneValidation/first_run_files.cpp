#include "SurrealEngine/RuneGameFiles.h"
#include <iostream>
namespace fs=std::filesystem;
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(int argc,char** argv){
    if(argc==3){RuneGameFiles::Import(argv[1],argv[2]);std::cout<<"PASS real game import\n";return 0;}
    auto root=fs::temp_directory_path()/("open-rune-setup-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        auto source=root/"source",dest=root/"game";
        auto write=[&](fs::path path,std::string data){fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f<<data;};
        const auto package=std::string("\xc1\x83\x2a\x9e")+std::string(64,'x');
        for(auto path:{"System/Core.u","System/Engine.u","System/RuneI.u","System/RMenu.u","Maps/DM-Hildir.run","Textures/a.utx","Meshes/a.ums","Sounds/a.uax","Music/a.umx"})write(source/path,package);
        write(source/"System/Rune.exe","test version identifier");
        write(source/"System/Default.ini","[Core.System]\nPaths=C:\\original\\*.u\nSavePath=C:\\original\\Save\nCachePath=C:\\original\\Cache\n");
        write(source/"System/DefUser.ini","[Engine.Input]\n");
        write(source/"System/User.ini","private player settings");write(source/"Save/Save1.usa","private save");write(source/"System/Engine.dll","native code");
        bool bad=false;try{RuneGameFiles::Import(root/"missing",dest);}catch(...){bad=true;}require(bad&&!fs::exists(dest),"Invalid import created game folder");
        size_t count=0;RuneGameFiles::Import(source/"System",dest,[&](size_t n,size_t total){count=n;require(n<=total,"Bad progress");});
        require(count==12,"Unexpected import file set");RuneGameFiles::Validate(dest);
        require(!fs::exists(dest/"System/User.ini")&&!fs::exists(dest/"Save/Save1.usa")&&!fs::exists(dest/"System/Engine.dll"),"Personal/native files copied");
        std::ifstream in(dest/"System/Default.ini");std::string config((std::istreambuf_iterator<char>(in)),{});in.close();
        require(config.find("C:\\original")==std::string::npos&&config.find("SavePath=..\\Save")!=std::string::npos,"Source paths retained");
        write(dest/"Save/Save1.usa","existing save");bad=false;try{RuneGameFiles::Import(source,dest);}catch(...){bad=true;}
        require(bad&&fs::exists(dest/"Save/Save1.usa"),"Existing install overwritten");
        auto interrupted=root/"interrupted";bad=false;try{RuneGameFiles::Import(source,interrupted,[](size_t,size_t){throw std::runtime_error("cancel");});}catch(...){bad=true;}
        require(bad&&!fs::exists(interrupted),"Failed copy left active data");
        for(const auto& entry:fs::directory_iterator(root))require(!entry.path().filename().string().starts_with("interrupted.importing-"),"Failed copy left staging data");
        write(source/"System/Core.u","invalid");bad=false;try{RuneGameFiles::Validate(source);}catch(...){bad=true;}require(bad,"Invalid package accepted");
        require(fs::exists(source/"Save/Save1.usa")&&fs::exists(source/"System/User.ini"),"Source files changed");
        fs::remove_all(root);std::cout<<"PASS setup: valid/invalid imports, progress, source/config/save preservation, existing destination, failed-copy rollback\n";
    }catch(...){fs::remove_all(root);throw;}
}
