#pragma once
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

// Game data only: imports never include saves, user configuration or native DLLs.
namespace RuneGameFiles {
namespace fs = std::filesystem;
inline constexpr const char* SteamURL = "https://store.steampowered.com/app/210950/Rune_Classic/";
inline std::string Lower(std::string s) {
    for (char& c:s) if(c>='A'&&c<='Z')c+=32;
    return s;
}
inline fs::path Child(const fs::path& parent,const std::string& name) {
    if(!fs::is_directory(parent))return {};
    for(const auto& entry:fs::directory_iterator(parent))
        if(Lower(entry.path().filename().string())==Lower(name))return entry.path();
    return {};
}
inline bool Package(const fs::path& file) {
    std::ifstream in(file,std::ios::binary); unsigned char magic[4]{};
    return bool(in.read(reinterpret_cast<char*>(magic),4)) &&
        magic[0]==0xc1&&magic[1]==0x83&&magic[2]==0x2a&&magic[3]==0x9e && fs::file_size(file)>32;
}
inline fs::path Root(fs::path selected) {
    if(Lower(selected.filename().string())=="system")selected=selected.parent_path();
    return fs::absolute(selected).lexically_normal();
}
inline bool Allowed(const std::string& folder,const std::string& name) {
    auto n=Lower(name),ext=Lower(fs::path(name).extension().string());
    if(folder=="System")return n=="rune.exe"||n=="default.ini"||n=="defuser.ini"||ext==".u"||ext==".int"||ext==".det"||ext==".frt"||ext==".est"||ext==".itt"||ext==".rut";
    if(folder=="Maps")return ext==".run";
    if(folder=="Textures")return ext==".utx";
    if(folder=="Meshes")return ext==".ums";
    if(folder=="Sounds")return ext==".uax";
    if(folder=="Music")return ext==".umx"||ext==".ogg";
    return false;
}
inline void Validate(const fs::path& root) {
    const auto system=Child(root,"System");
    for(const char* name:{"Core.u","Engine.u","RuneI.u","RMenu.u"})
        if(!Package(Child(system,name)))throw std::runtime_error(std::string("Missing or invalid System/")+name+". Choose the complete Rune Classic installation folder.");
    for(const char* name:{"Default.ini","DefUser.ini","Rune.exe"}) {
        auto p=Child(system,name);
        if(!fs::is_regular_file(p)||fs::file_size(p)==0)throw std::runtime_error(std::string("Missing System/")+name+". Choose the complete installation.");
    }
    if(!Package(Child(Child(root,"Maps"),"DM-Hildir.run")))throw std::runtime_error("Missing or invalid Maps/DM-Hildir.run.");
    for(const char* folder:{"Textures","Meshes","Sounds","Music"}) {
        bool found=false; auto dir=Child(root,folder);
        if(fs::is_directory(dir))for(const auto& entry:fs::directory_iterator(dir))
            if(entry.is_regular_file()&&Allowed(folder,entry.path().filename().string())&&fs::file_size(entry.path())>32) {found=true;break;}
        if(!found)throw std::runtime_error(std::string("Missing game data in ")+folder+".");
    }
}
inline std::string DefaultConfig(const std::string& text) {
    // Seed a fresh configuration with paths contained inside our own data folder.
    std::string out,section; bool paths=false; size_t at=0;
    while(at<text.size()) {
        auto end=text.find('\n',at); if(end==std::string::npos)end=text.size();
        auto line=text.substr(at,end-at); if(!line.empty()&&line.back()=='\r')line.pop_back();
        auto trim=[](std::string s){auto a=s.find_first_not_of(" \t");if(a==std::string::npos)return std::string();return s.substr(a,s.find_last_not_of(" \t")-a+1);};
        auto clean=Lower(trim(line)); if(clean.starts_with("["))section=clean;
        auto equals=clean.find('='); auto key=trim(clean.substr(0,equals));
        if(section=="[core.system]") {
            if(key=="savepath")line="SavePath=..\\Save";
            else if(key=="cachepath")line="CachePath=..\\Cache";
            else if(key=="paths") {
                if(!paths)out+="Paths=..\\System\\*.u\nPaths=..\\Maps\\*.run\nPaths=..\\Textures\\*.utx\nPaths=..\\Sounds\\*.uax\nPaths=..\\Music\\*.umx\nPaths=..\\Meshes\\*.ums\n";
                paths=true; at=end+1;continue;
            }
        }
        out+=line+"\n";at=end+1;
    }
    return out;
}
inline void Import(const fs::path& selected,const fs::path& destination,const std::function<void(size_t,size_t)>& progress={}) {
    auto source=Root(selected);Validate(source);
    if(fs::exists(destination))throw std::runtime_error("Open Rune's data folder already exists; it has been left unchanged.");
    // Stage beside the destination so promotion is atomic on the same volume.
    auto stage=destination;stage+=".importing-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    struct Item{fs::path from,to;};std::vector<Item> files;
    uintmax_t bytes=0;
    for(const char* folder:{"System","Maps","Textures","Meshes","Sounds","Music"}) {
        auto dir=Child(source,folder);
        for(const auto& entry:fs::directory_iterator(dir)) {
            if(fs::is_symlink(entry.symlink_status()))throw std::runtime_error("Linked game files are not supported. Select a folder containing the actual files.");
            if(!entry.is_regular_file()||!Allowed(folder,entry.path().filename().string()))continue;
            auto name=entry.path().filename().string();
            for(const char* canonical:{"Rune.exe","Core.u","Engine.u","RuneI.u","RMenu.u","Default.ini","DefUser.ini"})if(Lower(name)==Lower(canonical))name=canonical;
            files.push_back({entry.path(),fs::path(folder)/name});bytes+=entry.file_size();
        }
    }
    fs::create_directories(destination.parent_path());
    if(fs::space(destination.parent_path()).available<bytes+16*1024*1024)throw std::runtime_error("Not enough free space to copy the Rune game files.");
    if(!fs::create_directory(stage))throw std::runtime_error("Could not create a temporary import folder.");
    try {
        size_t done=0;
        for(const auto& file:files) {
            auto to=stage/file.to;fs::create_directories(to.parent_path());fs::copy_file(file.from,to);
            if(progress)progress(++done,files.size());
        }
        auto defaults=stage/"System/Default.ini";
        fs::permissions(defaults,fs::perms::owner_write,fs::perm_options::add);
        std::ifstream in(defaults,std::ios::binary);std::string text((std::istreambuf_iterator<char>(in)),{});in.close();
        {std::ofstream out(defaults,std::ios::binary|std::ios::trunc);out<<DefaultConfig(text);if(!out)throw std::runtime_error("Could not prepare game configuration.");}
        fs::create_directory(stage/"Save");fs::create_directory(stage/"Cache");Validate(stage);
        fs::rename(stage,destination);
    } catch(...) {std::error_code ec;fs::remove_all(stage,ec);throw;}
}
}
