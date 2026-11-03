#pragma once
#include "Audio/RuneSoundPack.h"
#include <chrono>

inline void ProbeRuneSoundPack(Engine& game,const std::string& name,const std::string& output){
    if(name.empty()||output.empty())throw std::runtime_error("Sound-pack probe needs --sound-probe-package and --sound-probe-output");
    auto* package=game.packages->GetPackage(name);auto result=JsonValue::object();result["sounds"]=JsonValue::array();
    auto begin=std::chrono::steady_clock::now();size_t bytes=0;
    for(int ref=1;ref<=package->GetExportCount();++ref){
        auto* entry=package->GetExportEntry(ref);int cls=entry->ObjClass;if(!cls)continue;
        auto className=package->GetName(cls<0?package->GetImportEntry(cls)->ObjName:package->GetExportEntry(cls)->ObjName).ToString();
        if(className!="Sound")continue;
        auto* sound=UObject::Cast<USound>(package->GetUObject(ref));sound->GetSound();bytes+=sound->samples.size()*sizeof(float);
        auto row=JsonValue::object();row["id"]=JsonValue::string(RuneSoundPack::Identity(sound));
        row["decoded_sha1"]=JsonValue::string(RuneSoundPack::Hash(sound->samples.data(),sound->samples.size()*sizeof(float)));
        row["samples"]=JsonValue::number(double(sound->samples.size()));row["rate"]=JsonValue::number(sound->frequency);row["channels"]=JsonValue::number(sound->channels);
        row["duration"]=JsonValue::number(sound->duration);row["looped"]=JsonValue::boolean(sound->loopInfo.Looped);
        row["start"]=JsonValue::number(double(sound->loopInfo.LoopStart));row["end"]=JsonValue::number(double(sound->loopInfo.LoopEnd));
        result["sounds"].items().push_back(std::move(row));
    }
    auto& state=RuneSoundPack::State();result["replaced"]=JsonValue::number(double(state.replaced));result["rejected"]=JsonValue::number(double(state.rejected));result["missing"]=JsonValue::number(double(state.missing));
    result["decoded_bytes"]=JsonValue::number(double(bytes));result["seconds"]=JsonValue::number(std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count());
    File::write_all_text(output,result.to_json(true));
}
