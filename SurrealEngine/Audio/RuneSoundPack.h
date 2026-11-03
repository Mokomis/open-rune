#pragma once
#include "Engine.h"
#include "Audio/AudioSource.h"
#include "Package/Package.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Utils/CommandLine.h"
#include "Utils/File.h"
#include "Utils/JsonValue.h"
#include "Utils/Logger.h"
#include "TinySHA1/TinySHA1.hpp"
#include <mutex>
#include <set>

namespace RuneSoundPack {
inline std::string Lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
inline std::string Hash(const void* bytes,size_t count){
    sha1::SHA1 hash;hash.processBytes(bytes,count);uint32_t words[5];hash.getDigest(words);char text[41];
    snprintf(text,sizeof(text),"%08x%08x%08x%08x%08x",words[0],words[1],words[2],words[3],words[4]);return text;
}
inline std::string Identity(USound* sound){
    auto* package=sound->package;if(!package)return {};
    std::string name;std::set<int> visited;
    for(int ref=int(sound->exportIndex)+1;ref>0;ref=package->GetExportEntry(ref)->ObjOuter){
        if(ref>package->GetExportCount()||!visited.insert(ref).second)return {};
        auto part=package->GetName(package->GetExportEntry(ref)->ObjName).ToString();name=part+(name.empty()?"":"."+name);
    }
    return package->GetPackageName().ToString()+"."+name;
}
struct Entry{fs::path file;std::string sourceHash,fileHash;int frames=0,rate=0,channels=0;bool loop=false;uint32_t start=0,end=0;};
struct Index{
    std::once_flag once;bool enabled=false;fs::path root;std::map<std::string,Entry> entries;
    size_t replaced=0,rejected=0,missing=0;
};
inline Index& State(){static Index index;return index;}
inline bool HexHash(const std::string& s){return s.size()==40&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
inline void Initialize(){
    auto& index=State();std::call_once(index.once,[&]{
        if(!engine||!commandline||!engine->LaunchInfo.IsRune())return;
        int mode=commandline->GetArgInt("","--sound-remaster",-1);
#ifdef WIN32
        if(mode<0)try{auto settings=JsonValue::parse(File::read_all_text((Directory::localAppData()/"SurrealEngine/DesktopMenu.json").string()));mode=settings["soundRemaster"].to_int();}catch(...){}
#endif
        index.enabled=mode==1;if(!index.enabled)return;
        try{
            index.root=fs::weakly_canonical(commandline->GetArg("","--sound-pack",(fs::path(engine->LaunchInfo.gameRootFolder)/"SoundPacks/OpenRune-Layered-v1").string()));
            auto manifest=index.root/"runtime.json";
            if(!fs::is_regular_file(manifest)||fs::file_size(manifest)>8*1024*1024)throw std::runtime_error("Pack index is missing or too large");
            auto json=JsonValue::parse(File::read_all_text(manifest.string()));
            if(json["schema"].to_int()!=1||json["assets"].type()!=JsonType::array||json["assets"].items().size()>20000)throw std::runtime_error("Unsupported pack index");
            for(auto& row:json["assets"].items()){
                auto id=Lower(row["id"].to_string());auto relative=fs::path(row["file"].to_string());
                if(id.empty()||relative.is_absolute()||relative.has_root_name()||relative.extension()!=".wav")throw std::runtime_error("Invalid pack path or identity");
                auto full=fs::weakly_canonical(index.root/relative);auto inside=full.lexically_relative(index.root);
                if(inside.empty()||*inside.begin()=="..")throw std::runtime_error("Pack path leaves its folder");
                Entry entry;entry.file=full;entry.sourceHash=row["source_sha1"].to_string();entry.fileHash=row["file_sha1"].to_string();
                entry.frames=row["frames"].to_int();entry.rate=row["sample_rate"].to_int();entry.channels=row["channels"].to_int();entry.loop=row["looped"].to_boolean();entry.start=row["loop_start"].to_int();entry.end=row["loop_end"].to_int();
                if(!HexHash(entry.sourceHash)||!HexHash(entry.fileHash)||entry.frames<=0||entry.rate<4000||entry.rate>192000||entry.frames>entry.rate*600||entry.channels<1||entry.channels>2)throw std::runtime_error("Invalid sound dimensions or hash");
                if(!index.entries.emplace(id,std::move(entry)).second)throw std::runtime_error("Duplicate sound identity");
            }
            LogMessage("RUNE_SOUND_PACK available="+std::to_string(index.entries.size())+" mode=layered");
        }catch(const std::exception& error){index.entries.clear();LogMessage(std::string("RUNE_SOUND_PACK fallback=original reason=")+error.what());}
    });
}
inline std::unique_ptr<AudioSource> Replacement(USound* sound,AudioSource& original){
    // Servers decode original durations without loading a cosmetic sound pack.
    bool probe=commandline&&commandline->HasArg("","--sound-pack-probe");
    if(!engine||!engine->LaunchInfo.IsRune()||(!probe&&(!engine->audiodev||!engine->audiodev->GetDevice())))return {};
    Initialize();auto& index=State();if(!index.enabled)return {};
    const auto id=Lower(Identity(sound));auto found=index.entries.find(id);
    if(found==index.entries.end()){++index.missing;return {};}
    try{
        const auto& entry=found->second;
        if(Hash(sound->Data.data(),sound->Data.size())!=entry.sourceHash)throw std::runtime_error("Original sound differs from pack source");
        if(entry.frames!=original.GetSamples()||entry.rate!=original.GetFrequency()||entry.channels!=original.GetChannels()||entry.loop!=original.bIsLooped||(entry.loop&&(entry.start!=original.loopStart||entry.end!=original.loopEnd)))throw std::runtime_error("Original timing or loops differ");
        if(!fs::is_regular_file(entry.file)||fs::file_size(entry.file)>32*1024*1024)throw std::runtime_error("Replacement is missing or too large");
        auto bytes=File::read_all_bytes(entry.file.string());
        if(Hash(bytes.data(),bytes.size())!=entry.fileHash)throw std::runtime_error("Replacement file hash differs");
        auto source=AudioSource::CreateWav(std::move(bytes));
        if(source->GetSamples()!=entry.frames||source->GetFrequency()!=entry.rate||source->GetChannels()!=entry.channels||source->bIsLooped!=entry.loop||(entry.loop&&(source->loopStart!=entry.start||source->loopEnd!=entry.end)))throw std::runtime_error("Replacement timing or loops differ");
        ++index.replaced;return source;
    }catch(const std::exception& error){++index.rejected;LogMessage("RUNE_SOUND_PACK rejected="+id+" reason="+error.what());return {};}
}
}
