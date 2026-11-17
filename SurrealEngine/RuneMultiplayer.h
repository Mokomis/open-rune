#pragma once
#include <string>
#include "openrune/net/PlayerIdentity.h"
class Engine;
// Private two-to-sixteen-player sessions. Invitations are local files containing
// per-player random credentials. Simulation and presentation use independent loops.
void RunRuneMultiplayerServer(Engine&,const std::string& invitationDirectory);
void RunRuneMultiplayerClient(Engine&,const std::string& invitationFile);

#include <atomic>
#include <mutex>
namespace RuneMultiplayer {
inline std::atomic<bool> Active{false}, Leave{false}, Respawn{false}, Waiting{false};
inline std::atomic<bool> Diagnostics{false},ShowPlayerNames{true};
inline std::atomic<bool> AdaptiveSmoothing{true};
// Android's foreground host lives in a separate process from its rendered client.
inline std::atomic<bool> HostStop{false}, HostReady{false};
inline std::mutex Mutex;
inline std::string LocalPlayerName;
inline std::string GetPlayerName(){std::lock_guard<std::mutex> lock(Mutex);return LocalPlayerName;}
inline bool SetPlayerName(std::string s){s=openrune::net::trimPlayerName(std::move(s));if(!openrune::net::validPlayerName(s))return false;std::lock_guard<std::mutex> lock(Mutex);LocalPlayerName=std::move(s);return true;}
inline std::string PendingInvite, Status="Private matches · Deathmatch, Team Deathmatch, Arena and HeadBall", HostFailure;
inline void SetHostFailure(const std::string& text) { std::lock_guard<std::mutex> lock(Mutex); HostFailure=text; }
inline std::string GetHostFailure() { std::lock_guard<std::mutex> lock(Mutex); return HostFailure; }
inline void SetStatus(const std::string& text) { std::lock_guard<std::mutex> lock(Mutex); Status=text; }
inline std::string GetStatus() { std::lock_guard<std::mutex> lock(Mutex); return Status; }
inline bool Join(const std::string& file) {
    std::lock_guard<std::mutex> lock(Mutex);
    if(Active.load() || file.empty() || file.size()>4096)return false;
    Active=true;Leave=false;Respawn=false;Waiting=false;PendingInvite=file;Status="Joining match...";return true;
}
inline std::string TakeInvite() { std::lock_guard<std::mutex> lock(Mutex); std::string out;out.swap(PendingInvite);return out; }
}

void RunRuneMultiplayerBot(Engine&,const std::string& invitationFile);
