#pragma once

#include <atomic>

// Android only queues this request. Map and actor changes run between frames.
namespace RuneCombat
{
inline std::atomic<bool> Ready{false};
inline std::atomic<bool> PracticeArena{false};
// 1=new campaign, 2=quick save, 3=quick load, 4=checkpoint, 5=new fight,
// 6=manual save (RuneMenu::Slot 0-8), 7=load selected slot (0-10), 8=restart practice fight.
inline std::atomic<int> SessionRequest{0};
// 9=isolated cinematic-physics playground.
// Completion feedback is consumed by the Android overlay.
inline std::atomic<int> SessionResult{0};
inline bool Request(int action)
{
	if (!Ready.load()) return false;
	if (action==8 && !PracticeArena.load()) return false;
	int empty=0;
	if (!SessionRequest.compare_exchange_strong(empty,action)) return false;
	Ready.store(false);
	return true;
}
}
