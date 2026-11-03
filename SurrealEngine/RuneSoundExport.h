#pragma once
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Engine/Resources/USound.h"
#include "Audio/AudioSource.h"
#include "Utils/JsonValue.h"
#include "Utils/File.h"
#include <bit>
#include <set>
#include <iostream>

// Offline extraction: no rendering, actor ticks, playback, or package writes.
inline void ExportRuneSounds(Engine& game,const std::string& name,const std::string& destination)
{
    if(name.empty()||destination.empty())throw std::runtime_error("Sound export requires --sound-package and --sound-output");
    const auto out=fs::absolute(destination);
    if(fs::exists(out))throw std::runtime_error("Sound export output must be a new directory");
    auto* pkg=game.packages->GetPackage(name);auto manifest=JsonValue::object();
    manifest["schema"]=JsonValue::number(1);manifest["package"]=JsonValue::string(name);
    manifest["source"]=JsonValue::string(pkg->GetPackageFilePath());manifest["sounds"]=JsonValue::array();
    fs::create_directories(out);std::set<std::string> identities;
    for(int ref=1;ref<=pkg->GetExportCount();++ref) {
        auto entry=pkg->GetExportEntry(ref);int cls=entry->ObjClass;if(!cls)continue;
        auto className=pkg->GetName(cls<0?pkg->GetImportEntry(cls)->ObjName:pkg->GetExportEntry(cls)->ObjName).ToString();
        if(className!="Sound")continue;
        std::string identity;std::set<int> visited;
        for(int cur=ref;cur>0;cur=pkg->GetExportEntry(cur)->ObjOuter) {
            if(!visited.insert(cur).second)throw std::runtime_error("Cyclic sound group");
            auto part=pkg->GetName(pkg->GetExportEntry(cur)->ObjName).ToString();
            if(part.empty()||part=="."||part==".."||part.find_first_of("/\\:")!=std::string::npos)throw std::runtime_error("Unsafe sound name");
            identity=part+(identity.empty()?"":"."+identity);
        }
        if(!identities.insert(identity).second)throw std::runtime_error("Duplicate sound identity");
        auto row=JsonValue::object();row["id"]=JsonValue::string(name+"."+identity);
        try {
            auto* sound=UObject::Cast<USound>(pkg->GetUObject(ref));auto format=sound->Format.ToString();
            row["format"]=JsonValue::string(format);row["raw_file"]=JsonValue::string(identity+".source."+format);
            File::write_all_bytes((out/(identity+".source."+format)).string(),sound->Data.data(),sound->Data.size());
            auto decoder=format=="wav"?AudioSource::CreateWav(sound->Data):format=="mp3"||format=="mp2"?AudioSource::CreateMp3(sound->Data):nullptr;
            if(!decoder)throw std::runtime_error("Unsupported source audio format");
            const int channels=decoder->GetChannels(),rate=decoder->GetFrequency(),frames=decoder->GetSamples();
            if(channels<1||channels>2||rate<4000||rate>192000||frames<=0||frames>rate*600)throw std::runtime_error("Invalid or oversized sound dimensions");
            std::vector<float> samples(size_t(frames)*channels);auto read=decoder->ReadSamples(samples.data(),samples.size());
            if(read!=samples.size())throw std::runtime_error("Incomplete audio decode");
            std::vector<uint8_t> wav;auto text=[&](const char* s){for(int i=0;i<4;++i)wav.push_back(s[i]);};
            auto u16=[&](uint16_t n){wav.push_back(n&255);wav.push_back(n>>8);};
            auto u32=[&](uint32_t n){for(int i=0;i<4;++i)wav.push_back((n>>(i*8))&255);};
            text("RIFF");u32(36+uint32_t(samples.size()*4));text("WAVE");text("fmt ");u32(16);u16(3);u16(uint16_t(channels));u32(rate);u32(rate*channels*4);u16(uint16_t(channels*4));u16(32);text("data");u32(uint32_t(samples.size()*4));
            for(float sample:samples){if(!std::isfinite(sample))throw std::runtime_error("Nonfinite decoded audio");u32(std::bit_cast<uint32_t>(sample));}
            File::write_all_bytes((out/(identity+".decoded.wav")).string(),wav.data(),wav.size());
            row["decoded_file"]=JsonValue::string(identity+".decoded.wav");row["frames"]=JsonValue::number(frames);
            row["sample_rate"]=JsonValue::number(rate);row["channels"]=JsonValue::number(channels);
            row["looped"]=JsonValue::boolean(decoder->bIsLooped);row["loop_start"]=JsonValue::number(decoder->loopStart);row["loop_end"]=JsonValue::number(decoder->loopEnd);
            row["status"]=JsonValue::string("exported");
        }catch(const std::exception& e){row["status"]=JsonValue::string("held");row["reason"]=JsonValue::string(e.what());}
        manifest["sounds"].items().push_back(std::move(row));
    }
    File::write_all_text((out/"manifest.json").string(),manifest.to_json(true));
    std::cout<<"RUNE_SOUND_EXPORT package="<<name<<" sounds="<<manifest["sounds"].items().size()<<"\n";
}
