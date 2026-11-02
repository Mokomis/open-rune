#pragma once

#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Packages/Engine/UViewport.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include <iostream>
#include <stdexcept>

// A backend spy verifies audio routing without opening a speaker/device. This
// validates native268's channel behavior, not audible playback on Android.
struct RuneAudioProbe
{
    struct Device : AudioDevice
    {
        std::vector<int> Stopped;
        int MusicChanges=0;
        void AddSound(USound*) override { }
        void RemoveSound(USound*) override { }
        bool IsPlaying(int) override { return true; }
        int GetTotalChannels() override { return 3; }
        void PlaySound(int,USound*,vec3&,float,float,float) override { }
        void PlayMusic(std::unique_ptr<AudioSource>) override { ++MusicChanges; }
        void UpdateSound(int,USound*,vec3&,float,float,float) override { }
        void StopSound(int channel) override { Stopped.push_back(channel); }
        void SetMusicVolume(float) override { }
        void SetSoundVolume(float) override { }
        void Update() override { }
    };

    static void Run(Engine& game)
    {
        auto require=[](bool ok,const char* why) { if(!ok) throw std::runtime_error(why); };
        USurrealAudioDevice audio("RuneAudioProbe",nullptr,ObjectFlags::Transient);
        UViewport viewport("RuneAudioProbeViewport",nullptr,ObjectFlags::Transient);
        auto device=std::make_unique<Device>(); auto* spy=device.get();
        audio.m_Device=std::move(device); audio.m_Viewport=&viewport; audio.CurrentSection=37;
        audio.PlayingSounds.resize(3);
        for(int i=0;i<3;i++) { audio.PlayingSounds[i].Id=16+i; audio.PlayingSounds[i].IsActive=i!=1; }
        struct Restore { Engine& Game; USurrealAudioDevice* Audio; ~Restore() { Game.audiodev=Audio; } } restore{game,game.audiodev};
        game.audiodev=&audio;
        auto* actor=game.packages->GetTransientPackage()->NewObject("RuneStopAllSoundActor",
            game.packages->GetPackage("Engine")->GetClass("Actor"),ObjectFlags::Transient);
        auto* native=FindEventFunction(actor,"StopAllSound");
        require(native!=nullptr,"StopAllSound declaration missing");
        Frame::Call(native,actor,{});
        require(spy->Stopped==std::vector<int>({0,2}),"StopAllSound did not stop every active effect channel");
        for(const auto& playing:audio.PlayingSounds)
            require(playing.Id==0 && !playing.IsActive && playing.Actor==nullptr && playing.Sound==nullptr,
                "StopAllSound retained queued/active sound state");
        require(spy->MusicChanges==0 && audio.m_Viewport==&viewport && audio.CurrentSection==37,
            "StopAllSound stopped music or disconnected viewport");
        Frame::Call(native,actor,{});
        require(spy->Stopped.size()==2 && spy->MusicChanges==0,"Repeated StopAllSound changed cleared audio");
        game.audiodev=nullptr;
        Frame::Call(native,actor,{});
        std::cout << "RUNE_AUDIO_STOP_ALL_OK active_stopped=2 queued_cleared=1 music_preserved=1 viewport_preserved=1 repeat=1 no_device=1\n";
    }
};
