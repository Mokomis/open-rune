// Synthetic impulse checks use our exact runtime EFX integration; no game/mod assets.
#include "SurrealEngine/Audio/EnvironmentalEffects.h"
#include <iostream>
#include <stdexcept>
#include <numeric>
#include <fstream>

std::vector<float> render(float wet, float obstruction)
{
    auto open=(LPALCLOOPBACKOPENDEVICESOFT)alcGetProcAddress(nullptr,"alcLoopbackOpenDeviceSOFT");
    auto samples=(LPALCRENDERSAMPLESSOFT)alcGetProcAddress(nullptr,"alcRenderSamplesSOFT");
    if(!open || !samples) throw std::runtime_error("No loopback extension");
    auto* device=open(nullptr);
    ALCint attrs[]={ALC_FORMAT_CHANNELS_SOFT,ALC_STEREO_SOFT,ALC_FORMAT_TYPE_SOFT,ALC_FLOAT_SOFT,ALC_FREQUENCY,48000,0};
    auto* ctx=alcCreateContext(device,attrs);
    if(!ctx || !alcMakeContextCurrent(ctx))throw std::runtime_error("No loopback context");
    EnvironmentalEffects effects;
    if(!effects.Init(device,1))throw std::runtime_error("EFX unavailable");
    effects.SetRoom(wet,1.5f,.7f);
    std::vector<float> input(4800,0);input[100]=0.5f;
    ALuint source,buffer;alGenSources(1,&source);alGenBuffers(1,&buffer);
    alBufferData(buffer,AL_FORMAT_MONO_FLOAT32,input.data(),input.size()*sizeof(float),48000);
    alSourcei(source,AL_BUFFER,buffer);alSourcei(source,AL_SOURCE_RELATIVE,AL_TRUE);
    alSourcef(source,AL_ROLLOFF_FACTOR,0);
    if(wet<0) effects.Route(source,0,1,true); // exercise live Original detachment
    effects.Route(source,0,obstruction,wet>=0);
    alSourcePlay(source);
    std::vector<float> result(48000*2*2);samples(device,result.data(),48000*2);
    if(alGetError()!=AL_NO_ERROR)throw std::runtime_error("OpenAL operation failed");
    alDeleteSources(1,&source);alDeleteBuffers(1,&buffer);effects.Clear();
    alcMakeContextCurrent(nullptr);alcDestroyContext(ctx);alcCloseDevice(device);
    return result;
}
int main()
{
    auto dry=render(-1,0),wet=render(.7f,0),muffled=render(0,1);
    auto energy=[](const auto& a,size_t start,size_t end){double e=0;for(size_t i=start;i<end;i++){if(!std::isfinite(a[i]))throw std::runtime_error("Nonfinite output");e+=a[i]*a[i];}return e;};
    double dt=energy(dry,24000,dry.size()),wt=energy(wet,24000,wet.size());
    double direct=energy(dry,0,24000),filtered=energy(muffled,0,24000);
    if(!(wt>1e-9 && wt>dt*100 && filtered>0 && filtered<direct*.7))throw std::runtime_error("Reverb/filter PCM assertion failed");
    std::cout<<"RUNE_AUDIO_EFX_PCM_OK dry_tail="<<dt<<" wet_tail="<<wt<<" direct_energy="<<direct<<" filtered_energy="<<filtered<<" original_detach=1\n";
}
