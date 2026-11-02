#pragma once
#ifdef __ANDROID__
// Render thread only. Negative values preserve the existing selection.
void AndroidFramePacingConfigure(int targetFps, int enabled);
bool AndroidFramePacingActive();
#endif
