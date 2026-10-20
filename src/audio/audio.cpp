#include "audio.h"
#include <SDL.h>
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#include "stb_vorbis.c"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
namespace Audio {
struct Clip { std::vector<float> pcm; bool music=false; };
struct Voice { uint64_t key; std::shared_ptr<Clip> clip; size_t pos=0; bool loop=false, paused=false; };
static SDL_AudioDeviceID device=0;
static SDL_AudioSpec spec{};
static std::map<uint64_t,std::shared_ptr<Clip>> clips;
static std::vector<Voice> voices;
static int musicVolume=5, soundVolume=5;
static uint64_t mixedSamples=0, nonzeroSamples=0;
static float peak=0;
static std::vector<float> capture;
static size_t captured=0;
static void mix(void*, Uint8* bytes, int len) {
    memset(bytes,0,len);
    float* output=reinterpret_cast<float*>(bytes);
    const size_t count=len/sizeof(float);
    for(auto& v: voices) {
        if(v.paused || v.clip->pcm.empty()) continue;
        float gain=(v.clip->music?musicVolume:soundVolume)/5.0f;
        for(size_t i=0;i<count;i++) {
            if(v.pos==v.clip->pcm.size()) { if(v.loop) v.pos=0; else break; }
            output[i]+=v.clip->pcm[v.pos++]*gain;
        }
    }
    voices.erase(std::remove_if(voices.begin(),voices.end(),[](const Voice& v){return !v.loop && v.pos==v.clip->pcm.size();}),voices.end());
    for(size_t i=0;i<count;i++) {
        output[i]=std::clamp(output[i],-1.0f,1.0f);
        peak=std::max(peak,std::abs(output[i])); nonzeroSamples+=(output[i]!=0);mixedSamples++;
    }
    size_t take=std::min(count,capture.size()-captured);
    if(take) { memcpy(capture.data()+captured,output,take*sizeof(float));captured+=take; }
}
bool init() {
    if(device) return true;
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0) { fprintf(stderr,"[audio] init failed: %s\n",SDL_GetError()); return false; }
    SDL_AudioSpec want{}; want.freq=48000;want.format=AUDIO_F32SYS;want.channels=2;want.samples=1024;want.callback=mix;
    device=SDL_OpenAudioDevice(nullptr,0,&want,&spec,SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if(!device) { fprintf(stderr,"[audio] open failed: %s\n",SDL_GetError()); return false; }
    if(getenv("HL5_CAPTURE_AUDIO")) capture.resize(spec.freq*2*30);
    SDL_PauseAudioDevice(device,0);
    fprintf(stderr,"[audio] ready %d Hz stereo float\n",spec.freq);
    return true;
}
void shutdown() {
    if(device) { SDL_CloseAudioDevice(device); device=0; }
    fprintf(stderr,"[audio] mixer samples=%llu nonzero=%llu peak=%.4f\n",(unsigned long long)mixedSamples,(unsigned long long)nonzeroSamples,peak);
    if(const char* path=getenv("HL5_CAPTURE_AUDIO")) {
        FILE* f=fopen(path,"wb");
        if(f) {
            uint8_t header[44]={}; memcpy(header,"RIFF",4);memcpy(header+8,"WAVEfmt ",8);memcpy(header+36,"data",4);
            auto w32=[&](int at,uint32_t v){memcpy(header+at,&v,4);};auto w16=[&](int at,uint16_t v){memcpy(header+at,&v,2);};
            w32(4,36+(uint32_t)captured*4);w32(16,16);w16(20,3);w16(22,2);w32(24,spec.freq);w32(28,spec.freq*8);w16(32,8);w16(34,32);w32(40,(uint32_t)captured*4);
            fwrite(header,1,44,f);fwrite(capture.data(),4,captured,f);fclose(f);
        }
    }
    voices.clear();clips.clear();capture.clear();
}
int load(const std::string& vfs,uint64_t key,uint64_t offset,uint64_t length,bool music) {
    if(!device || !length || length>32*1024*1024) return -1;
    FILE* f=fopen(vfs.c_str(),"rb"); if(!f) return -1;
    if(fseek(f,0,SEEK_END)!=0) { fclose(f); return -1; }
    long size=ftell(f);
    if(size<0 || offset>uint64_t(size) || length>uint64_t(size)-offset) { fclose(f);return -1; }
    if(fseek(f,(long)offset,SEEK_SET)!=0) { fclose(f);return -1; }
    std::vector<uint8_t> encoded((size_t)length);
    size_t got=fread(encoded.data(),1,encoded.size(),f); fclose(f); if(got!=encoded.size()) return -1;
    drmp3_config config{}; drmp3_uint64 frames=0;
    float* decoded=nullptr;
    if(encoded.size()>=4 && memcmp(encoded.data(),"OggS",4)==0) {
        int error=0;
        stb_vorbis* vorbis=stb_vorbis_open_memory(encoded.data(),(int)encoded.size(),&error,nullptr);
        if(vorbis) {
            stb_vorbis_info info=stb_vorbis_get_info(vorbis);
            config.channels=info.channels;config.sampleRate=info.sample_rate;
            std::vector<float> pcm; float chunk[8192]; int n;
            while((n=stb_vorbis_get_samples_float_interleaved(vorbis,info.channels,chunk,8192))>0) {
                if(pcm.size()+(size_t)n*info.channels>32*1024*1024) { pcm.clear();break; }
                pcm.insert(pcm.end(),chunk,chunk+n*info.channels);
            }
            stb_vorbis_close(vorbis);
            frames=pcm.size()/info.channels;
            if(frames) { decoded=(float*)malloc(pcm.size()*sizeof(float)); if(decoded)memcpy(decoded,pcm.data(),pcm.size()*sizeof(float)); }
        }
    } else decoded=drmp3_open_memory_and_read_pcm_frames_f32(encoded.data(),encoded.size(),&config,&frames,nullptr);
    if(!decoded || !frames) { if(decoded) drmp3_free(decoded,nullptr); fprintf(stderr,"[audio] decode failed key=%llu offset=%llu len=%llu header=%02x%02x%02x%02x\n",(unsigned long long)key,(unsigned long long)offset,(unsigned long long)length,encoded[0],encoded.size()>1?encoded[1]:0,encoded.size()>2?encoded[2]:0,encoded.size()>3?encoded[3]:0); return -1; }
    SDL_AudioCVT cvt{};
    int result=SDL_BuildAudioCVT(&cvt,AUDIO_F32SYS,(Uint8)config.channels,config.sampleRate,AUDIO_F32SYS,2,spec.freq);
    const size_t decodedBytes=(size_t)frames*config.channels*sizeof(float);
    if(result<0 || decodedBytes>128*1024*1024) { drmp3_free(decoded,nullptr);return -1; }
    auto clip=std::make_shared<Clip>();clip->music=music;
    if(result==0) clip->pcm.assign(decoded,decoded+frames*config.channels);
    else {
        std::vector<Uint8> converted(decodedBytes*cvt.len_mult);
        memcpy(converted.data(),decoded,decodedBytes);cvt.buf=converted.data();cvt.len=(int)decodedBytes;
        if(SDL_ConvertAudio(&cvt)!=0) { drmp3_free(decoded,nullptr);return -1; }
        clip->pcm.resize(cvt.len_cvt/sizeof(float));memcpy(clip->pcm.data(),converted.data(),cvt.len_cvt);
    }
    drmp3_free(decoded,nullptr);
    SDL_LockAudioDevice(device);clips[key]=clip;SDL_UnlockAudioDevice(device);
    fprintf(stderr,"[audio] loaded %s key=%llu seconds=%.2f\n",music?"music":"effect",(unsigned long long)key,clip->pcm.size()/(2.0*spec.freq));
    return 0;
}
int play(uint64_t key,bool loop) {
    if(!device) return -1;
    SDL_LockAudioDevice(device);auto it=clips.find(key);
    if(it==clips.end()){SDL_UnlockAudioDevice(device);return -1;}
    auto clip=it->second;
    voices.erase(std::remove_if(voices.begin(),voices.end(),[&](const Voice& v){return v.key==key || (clip->music && v.clip->music);}),voices.end());
    if(voices.size()>=32)voices.erase(voices.begin());
    voices.push_back({key,clip,0,loop,false});SDL_UnlockAudioDevice(device);return 0;
}
int stop(uint64_t key) { if(!device)return -1;SDL_LockAudioDevice(device);voices.erase(std::remove_if(voices.begin(),voices.end(),[&](const Voice& v){return v.key==key;}),voices.end());SDL_UnlockAudioDevice(device);return 0; }
int pause(uint64_t key) { if(!device)return -1;SDL_LockAudioDevice(device);for(auto& v:voices)if(v.key==key)v.paused=true;SDL_UnlockAudioDevice(device);return 0; }
int resume(uint64_t key) { if(!device)return -1;SDL_LockAudioDevice(device);for(auto& v:voices)if(v.key==key)v.paused=false;SDL_UnlockAudioDevice(device);return 0; }
int free(uint64_t key) { stop(key);if(!device)return -1;SDL_LockAudioDevice(device);clips.erase(key);SDL_UnlockAudioDevice(device);return 0; }
void volume(bool music,int value) { if(device)SDL_LockAudioDevice(device);(music?musicVolume:soundVolume)=std::clamp(value,0,5);if(device)SDL_UnlockAudioDevice(device); }
int volume(bool music) { return music?musicVolume:soundVolume; }
}
