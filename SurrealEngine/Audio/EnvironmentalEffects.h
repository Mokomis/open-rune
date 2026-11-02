#pragma once
// Original integration around the existing OpenAL EFX API. No mod code/presets.
#ifndef AL_ALEXT_PROTOTYPES
#define AL_ALEXT_PROTOTYPES
#endif
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>
#include <algorithm>
#include <cmath>
#include <vector>

class EnvironmentalEffects
{
public:
    bool Init(ALCdevice* device, size_t channels)
    {
        if (!alcIsExtensionPresent(device, "ALC_EXT_EFX")) return false;
        alGenEffects(1, &effect);
        alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
        alEffectf(effect, AL_REVERB_GAIN, 0.22f);
        alEffectf(effect, AL_REVERB_DENSITY, 0.8f);
        alEffectf(effect, AL_REVERB_DIFFUSION, 0.85f);
        alGenAuxiliaryEffectSlots(1, &slot);
        alAuxiliaryEffectSlotf(slot, AL_EFFECTSLOT_GAIN, 0.0f);
        alAuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, effect);
        filters.resize(channels);
        alGenFilters((ALsizei)channels, filters.data());
        for (auto filter : filters) alFilteri(filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        available = alGetError() == AL_NO_ERROR;
        return available;
    }
    // Destroy only after all attached sources have been destroyed.
    void Clear()
    {
        if (!filters.empty()) alDeleteFilters((ALsizei)filters.size(), filters.data());
        filters.clear();
        if (slot) alDeleteAuxiliaryEffectSlots(1, &slot);
        if (effect) alDeleteEffects(1, &effect);
        slot = effect = 0; available = false;
    }
    void SetRoom(float wet, float decay, float hf)
    {
        if (!available) return;
        wet = std::clamp(wet, 0.0f, 1.0f);
        decay = std::clamp(decay, 0.1f, 4.0f);
        hf = std::clamp(hf, 0.05f, 1.0f);
        if (decay != lastDecay || hf != lastHF)
        {
            alEffectf(effect, AL_REVERB_DECAY_TIME, decay);
            alEffectf(effect, AL_REVERB_GAINHF, hf);
            alEffectf(effect, AL_REVERB_DECAY_HFRATIO, 0.65f);
            alEffectf(effect, AL_REVERB_REFLECTIONS_GAIN, 0.12f);
            alEffectf(effect, AL_REVERB_LATE_REVERB_GAIN, 1.1f);
            alAuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, effect);
            lastDecay = decay; lastHF = hf;
        }
        alAuxiliaryEffectSlotf(slot, AL_EFFECTSLOT_GAIN, wet);
    }
    void Route(ALuint source, size_t channel, float obstruction, bool enabled)
    {
        if (!available || channel >= filters.size()) return;
        float amount = enabled ? std::clamp(obstruction, 0.0f, 1.0f) : 0.0f;
        alFilterf(filters[channel], AL_LOWPASS_GAIN, 1.0f - 0.35f * amount);
        alFilterf(filters[channel], AL_LOWPASS_GAINHF, 1.0f - 0.88f * amount);
        alSourcei(source, AL_DIRECT_FILTER, amount > 0 ? filters[channel] : AL_FILTER_NULL);
        alSource3i(source, AL_AUXILIARY_SEND_FILTER, enabled ? slot : AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
    }
    bool Available() const { return available; }
private:
    ALuint effect = 0, slot = 0;
    std::vector<ALuint> filters;
    bool available = false;
    float lastDecay = -1, lastHF = -1;
};
