#include "RuneBattleLaws.h"
#include "RunePhysics.h"
#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "Utils/JsonValue.h"
class Engine;
struct RuneDesktopHost {
    std::string name="Open Rune match",mode="dm",address="127.0.0.1";
    int bots=0,players=4,teams=2,arenaSize=1,score=20,minutes=10;
    int physics=RunePhysics::DefaultMode;
    uint32_t laws=RuneBattleLaws::All;
    bool publicGame=true,limbs=false,normalBloodlust=true;
    std::vector<std::string> maps;
    static bool Supports(const std::string&,const std::string&,int,int);
    std::vector<std::string> Available(const Engine&) const;
    void Normalize(const Engine&);
};
class RuneDesktopSession {
public:
    RuneDesktopSession();~RuneDesktopSession();
    void Host(Engine&,RuneDesktopHost config);
    void AddBot();void RemoveBot();int BotCount()const;
    void Browse();void Join(const std::string& key);void Cancel();void StopHost();void Leave();
    void Tick();
    bool Hosting()const;bool Busy()const;
    std::string TakeInvite();
    std::string Message="Host a match or find a game.";
    std::string LastKey,HostKey;
    JsonValue Rows=JsonValue::array();
    std::filesystem::path InvitationDirectory;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
