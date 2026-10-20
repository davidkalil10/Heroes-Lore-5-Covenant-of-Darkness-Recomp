#include "hle.h"
#include "../audio/audio.h"
#include "../core/trans.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#ifndef O_BINARY
#define O_BINARY _O_BINARY
#endif
#else
#include <unistd.h>
#include <sys/time.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif
#include <map>
#include <sys/stat.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) do { __android_log_print(ANDROID_LOG_INFO, "HL5", __VA_ARGS__); fprintf(stderr, __VA_ARGS__); } while(0)
#define LOGE(...) do { __android_log_print(ANDROID_LOG_ERROR, "HL5", __VA_ARGS__); fprintf(stderr, __VA_ARGS__); } while(0)
#else
#define LOGI(...) fprintf(stderr, __VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

// ---------------------------------------------------------------- globals
static Game* G = nullptr;
static std::string g_apkDir, g_dataDir;
static u32 g_errnoAddr = 0, g_stackGuard = 0;
static const int VFS_FAKE_FD = 900;
static u64 g_vfsLen = 0;

static float bf(u32 b) { float f; memcpy(&f, &b, 4); return f; }
static double bd(u32 lo, u32 hi) { u64 v = ((u64)hi << 32) | lo; double d; memcpy(&d, &v, 8); return d; }
static void retd(Cpu& c, double d) { u64 v; memcpy(&v, &d, 8); c.r[0] = (u32)v; c.r[1] = (u32)(v >> 32); }

// ---------------------------------------------------------------- printf on guest varargs
struct ArgReader {
    Cpu* cpu = nullptr; u32 ap = 0; int regIdx = -1; // regIdx>=0: reading from registers (variadic call) then stack
    u32 n32() {
        if (regIdx >= 0 && regIdx < 4) return cpu->r[regIdx++];
        if (regIdx >= 0) { u32 v = gm::r32(cpu->r[13] + 4 * (regIdx - 4)); regIdx++; return v; }
        u32 v = gm::r32(ap); ap += 4; return v;
    }
    u64 n64() {
        if (regIdx >= 0) { if (regIdx & 1) regIdx++; u32 lo = n32(); u32 hi = n32(); return ((u64)hi << 32) | lo; }
        ap = (ap + 7) & ~7u; u64 v = (u64)gm::r32(ap) | ((u64)gm::r32(ap + 4) << 32); ap += 8; return v;
    }
};

static std::string guestFormat(const std::string& fmt, ArgReader& a) {
    std::string out;
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] != '%') { out.push_back(fmt[i]); continue; }
        std::string spec = "%";
        i++;
        while (i < fmt.size() && strchr("-+ #0", fmt[i])) spec.push_back(fmt[i++]);
        while (i < fmt.size() && (isdigit((unsigned char)fmt[i]) || fmt[i] == '*')) {
            if (fmt[i] == '*') spec += std::to_string((int)a.n32()); else spec.push_back(fmt[i]);
            i++;
        }
        if (i < fmt.size() && fmt[i] == '.') {
            spec.push_back(fmt[i++]);
            while (i < fmt.size() && (isdigit((unsigned char)fmt[i]) || fmt[i] == '*')) {
                if (fmt[i] == '*') spec += std::to_string((int)a.n32()); else spec.push_back(fmt[i]);
                i++;
            }
        }
        int lng = 0;
        while (i < fmt.size() && strchr("hlLzjt", fmt[i])) { if (fmt[i] == 'l' || fmt[i] == 'z') lng++; i++; }
        if (i >= fmt.size()) break;
        char c = fmt[i];
        char buf[512];
        switch (c) {
            case '%': out.push_back('%'); break;
            case 'd': case 'i': {
                if (lng >= 2) { snprintf(buf, sizeof buf, (spec + "lld").c_str(), (long long)a.n64()); }
                else snprintf(buf, sizeof buf, (spec + "d").c_str(), (int)a.n32());
                out += buf; break; }
            case 'u': case 'x': case 'X': case 'o': {
                if (lng >= 2) { snprintf(buf, sizeof buf, (spec + "ll" + c).c_str(), (unsigned long long)a.n64()); }
                else snprintf(buf, sizeof buf, (spec + c).c_str(), (unsigned)a.n32());
                out += buf; break; }
            case 'c': snprintf(buf, sizeof buf, (spec + "c").c_str(), (int)a.n32()); out += buf; break;
            case 'p': snprintf(buf, sizeof buf, "0x%x", a.n32()); out += buf; break;
            case 's': { u32 p = a.n32(); std::string s = p ? gm::cstr(p) : "(null)"; snprintf(buf, sizeof buf, (spec + "s").c_str(), s.c_str()); out += buf; break; }
            case 'f': case 'g': case 'e': case 'G': case 'E': { u64 v = a.n64(); double d; memcpy(&d, &v, 8); snprintf(buf, sizeof buf, (spec + c).c_str(), d); out += buf; break; }
            default: out.push_back('%'); out.push_back(c);
        }
    }
    return out;
}

static u64 g_simTimeMs = 1600000000000ULL;

// ---------------------------------------------------------------- libc file layer
std::string g_overrideDir;
static std::map<u32, FILE*> g_files;  // guest FILE* -> host FILE*
static std::string vfsPath() {
    std::string sep = (g_apkDir.empty() || g_apkDir.back() == '/') ? "" : "/";
    std::string p1 = g_apkDir + sep + "data.vfs.mp3";
    FILE* f1 = fopen(p1.c_str(), "rb");
    if (f1) { fclose(f1); return p1; }

    std::string p2 = g_apkDir + sep + "assets/data.vfs.mp3";
    FILE* f2 = fopen(p2.c_str(), "rb");
    if (f2) { fclose(f2); return p2; }

    return p1;
}

// Index immutable VFS metadata once instead of reopening and scanning the archive
// for every asset lookup. Preserve first-match hash semantics and relative offsets.
bool fastVfsLookup(u32 manager,u32 name,u32 outOffset,u32& size) {
    if(getenv("HL5_NO_VFS_CACHE")) return false;
    struct Entry { u32 offset,size; };
    static std::map<u32,Entry> index;
    static u32 indexedStart=~0u,indexedLength=0;
    u32 start=gm::r32(manager+0x614),length=gm::r32(manager+0x618);
    if(start!=indexedStart || length!=indexedLength) {
        FILE* f=fopen(vfsPath().c_str(),"rb");if(!f)return false;
        if(fseek(f,0,SEEK_END)!=0) {fclose(f);return false;}
        long fileLength=ftell(f);
        if(fileLength<0 || start>(u64)fileLength || length>(u64)fileLength-start || length>64*1024*1024) {fclose(f);return false;}
        std::vector<u8> bytes(length);fseek(f,start,SEEK_SET);
        size_t n=fread(bytes.data(),1,length,f);fclose(f);if(n!=length)return false;
        std::map<u32,Entry> next;
        u32 pos=0;
        while(pos+8<=length) {
            u32 hash=0,entrySize=0;memcpy(&hash,bytes.data()+pos,4);memcpy(&entrySize,bytes.data()+pos+4,4);
            if(entrySize>length-pos-8)return false;
            next.emplace(hash,Entry{pos+8,entrySize});pos+=8+entrySize;
        }
        if(pos!=length)return false;
        index.swap(next);indexedStart=start;indexedLength=length;
        fprintf(stderr,"[vfs-cache] indexed %zu resources (%u bytes)\n",index.size(),length);
    }
    u32 hash=5381;for(unsigned char c:gm::cstr(name))hash=hash*33+c;
    auto it=index.find(hash);
    size=~0u;
    if(it!=index.end()) {size=it->second.size;gm::w32(outOffset,it->second.offset);}
    return true;
}

// ---------------------------------------------------------------- GLES1 software compositor
struct Tex { int w = 0, h = 0; u32 fmt = 0, type = 0; std::vector<u8> d; };
static std::map<u32, Tex> g_tex;
static u32 g_nextTex = 1, g_bound = 0;
static float g_ortho[4] = {0, 0, 0, 0};  // l r b t
static u32 g_vtxPtr = 0, g_vtxType = 0, g_vtxSize = 2, g_vtxStride = 0;
static u32 g_tcPtr = 0, g_tcType = 0, g_tcSize = 2, g_tcStride = 0;

static int bppOf(u32 fmt, u32 type) {
    if (type == 0x8363 || type == 0x8033 || type == 0x8034) return 2;
    switch (fmt) { case 0x1907: return 3; case 0x1908: return 4; case 0x1909: return 1; case 0x190A: return 2; }
    return 4;
}
static float readComp(u32 addr, u32 type) {
    if (type == 0x1406) return bf(gm::r32(addr));
    if (type == 0x1402) return (float)(int16_t)gm::r16(addr);
    if (type == 0x140C) return (float)(s32)gm::r32(addr) / 65536.0f;
    if (type == 0x1400) return (float)(int8_t)gm::r8(addr);
    return 0;
}
static u32 compSize(u32 type) { return type == 0x1406 || type == 0x140C ? 4 : type == 0x1402 ? 2 : 1; }

static u32 texel(const Tex& t, int x, int y) {
    if (x < 0 || y < 0 || x >= t.w || y >= t.h) return 0;
    int bpp = bppOf(t.fmt, t.type);
    const u8* p = &t.d[((size_t)y * t.w + x) * bpp];
    if (bpp == 2 && t.type == 0x8363) {
        u16 v = p[0] | (p[1] << 8);
        u32 r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
        return 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
    }
    if (bpp == 4) return ((u32)p[3] << 24) | ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
    if (bpp == 3) return 0xFF000000u | ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
    return 0xFF000000u;
}

static void glDraw(u32 mode, u32 first, u32 count) {
    auto it = g_tex.find(g_bound);
    if (it == g_tex.end() || count < 3 || !g_vtxPtr || !g_tcPtr) return;
    const Tex& t = it->second;
    u32 vs = g_vtxStride ? g_vtxStride : g_vtxSize * compSize(g_vtxType);
    u32 ts = g_tcStride ? g_tcStride : g_tcSize * compSize(g_tcType);
    Screen& s = G->screen;
    if (s.w <= 0) return;
    // ortho is (l,r,b,t): map GL y to screen row (row 0 = top). bottom<top means y-up.
    bool yUp = g_ortho[3] > g_ortho[2];
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    float uTL = 0, vTL = 0, uBR = 0, vBR = 0, bestTL = 1e30f, bestBR = -1e30f;
    static int drawCount = 0;
    if (drawCount++ < 10) {
        LOGI("[glDraw] #%d: mode=%u first=%u count=%u vtxType=0x%x tcType=0x%x yUp=%d bound=%u\n", drawCount, mode, first, count, g_vtxType, g_tcType, yUp, g_bound);
    }
    for (u32 i = first; i < first + count && i < first + 4; i++) {
        float gx = readComp(g_vtxPtr + i * vs, g_vtxType), gy = readComp(g_vtxPtr + i * vs + compSize(g_vtxType), g_vtxType);
        float u = readComp(g_tcPtr + i * ts, g_tcType), v = readComp(g_tcPtr + i * ts + compSize(g_tcType), g_tcType);
        float x = gx - g_ortho[0], y = yUp ? (g_ortho[3] - gy) : (gy - g_ortho[3]);
        if (drawCount <= 5) {
            fprintf(stderr, "   v%u: pos=(%f, %f) -> screen=(%f, %f), uv=(%f, %f)\n", i, gx, gy, x, y, u, v);
        }
        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        if (x + y < bestTL) { bestTL = x + y; uTL = u; vTL = v; }
        if (x + y > bestBR) { bestBR = x + y; uBR = u; vBR = v; }
    }
    float u0 = uTL, v0 = vTL, u1 = uBR, v1 = vBR;
    int dx0 = (int)floorf(x0 + 0.5f), dx1 = (int)floorf(x1 + 0.5f), dy0 = (int)floorf(y0 + 0.5f), dy1 = (int)floorf(y1 + 0.5f);
    float dw = (float)(dx1 - dx0), dh = (float)(dy1 - dy0);
    if (drawCount <= 5) {
        fprintf(stderr, "   bbox: dx0=%d dx1=%d dy0=%d dy1=%d dw=%f dh=%f u0=%f v0=%f u1=%f v1=%f\n", dx0, dx1, dy0, dy1, dw, dh, u0, v0, u1, v1);
        // sample center pixel of texture
        int cx = t.w / 2, cy = t.h / 2;
        fprintf(stderr, "   tex sample [0,0]=0x%08x [cx,cy]=0x%08x [100,100]=0x%08x\n", texel(t, 0, 0), texel(t, cx, cy), texel(t, 100, 100));
    }
    if (dw <= 0 || dh <= 0) return;
    int bpp = bppOf(t.fmt, t.type);
    int yStart = std::max(dy0, 0), yEnd = std::min(dy1, s.h);
    int xStart = std::max(dx0, 0), xEnd = std::min(dx1, s.w);
    if (xStart >= xEnd || yStart >= yEnd || t.d.empty()) return;

    float stepU = ((u1 - u0) / dw) * (float)t.w;
    float stepV = ((v1 - v0) / dh) * (float)t.h;

    int32_t du_fp = (int32_t)(stepU * 65536.0f);
    int32_t dv_fp = (int32_t)(stepV * 65536.0f);

    int32_t v_curr = (int32_t)((v0 * (float)t.h + ((yStart - dy0) + 0.5f) * stepV) * 65536.0f);
    int32_t u_base = (int32_t)((u0 * (float)t.w + ((xStart - dx0) + 0.5f) * stepU) * 65536.0f);

    const u8* texData = t.d.data();
    int tw = t.w, th = t.h;

    for (int y = yStart; y < yEnd; y++) {
        int ty = v_curr >> 16;
        v_curr += dv_fp;
        if ((unsigned)ty >= (unsigned)th) continue;

        int32_t u_curr = u_base;
        u32* dstRow = &s.px[(size_t)y * s.w];
        const u8* rowPtr = texData + ((size_t)ty * tw) * bpp;

        for (int x = xStart; x < xEnd; x++) {
            int tx = u_curr >> 16;
            u_curr += du_fp;
            if ((unsigned)tx >= (unsigned)tw) continue;

            u32 c;
            if (bpp == 4) {
                const u8* p = rowPtr + tx * 4;
                u32 a = p[3];
                if (a == 0) continue;
                c = ((u32)a << 24) | ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
                if (a != 255) {
                    u32 dst = dstRow[x];
                    u32 invA = 255 - a;
                    u32 r = (((c >> 16) & 255) * a + ((dst >> 16) & 255) * invA) / 255;
                    u32 g = (((c >> 8) & 255) * a + ((dst >> 8) & 255) * invA) / 255;
                    u32 b = ((c & 255) * a + (dst & 255) * invA) / 255;
                    dstRow[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
                    continue;
                }
            } else if (bpp == 2 && t.type == 0x8363) {
                const u8* p = rowPtr + tx * 2;
                u16 v = p[0] | (p[1] << 8);
                u32 r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
                c = 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
            } else if (bpp == 3) {
                const u8* p = rowPtr + tx * 3;
                c = 0xFF000000u | ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
            } else {
                c = 0xFF000000u;
            }
            dstRow[x] = c | 0xFF000000u;
        }
    }
    s.draws++;
}

// ---------------------------------------------------------------- JNI + Java emulation
struct JObj { int k = 0; std::string s; std::vector<u8> b; };  // k: 1 class, 2 string, 3 bytes, 4 fd, 5 other
static std::vector<JObj> g_objs;
static std::vector<u32> g_objFree;
static u32 newObj(JObj o) {
    if (!g_objFree.empty() && (o.k == 2 || o.k == 3)) { u32 i = g_objFree.back(); g_objFree.pop_back(); g_objs[i] = std::move(o); return 0x100 + i; }
    g_objs.push_back(std::move(o));
    return 0x100 + (u32)g_objs.size() - 1;
}
static JObj* obj(u32 h) { if (h < 0x100 || h - 0x100 >= g_objs.size()) return nullptr; return &g_objs[h - 0x100]; }
static std::map<std::string, u32> g_classes;
struct JM { std::string cls, name, sig; };
static std::vector<JM> g_methods;
static std::map<std::string, int> g_assetIds;
static std::vector<std::string> g_assetNames;

static u32 javaStatic(const std::string& name, const std::string& sig, std::vector<u64>& a, u32& hi);

static u32 stringObj(const std::string& s) { JObj o; o.k = 2; o.s = s; return newObj(std::move(o)); }

static u32 javaStatic(const std::string& name, const std::string& sig, std::vector<u64>& a, u32& hi) {
    hi = 0;
    auto asset = [&](const std::string& n) -> std::string {
        if (!g_overrideDir.empty()) {
            std::string o = g_overrideDir + "/" + n;
            FILE* fo = fopen(o.c_str(), "rb");
            if (fo) { fclose(fo); return o; }
        }
        std::string sep = (g_apkDir.empty() || g_apkDir.back() == '/') ? "" : "/";
        std::string p1 = g_apkDir + sep + n;
        FILE* f1 = fopen(p1.c_str(), "rb");
        if (f1) { fclose(f1); return p1; }
        std::string p2 = g_apkDir + sep + "assets/" + n;
        FILE* f2 = fopen(p2.c_str(), "rb");
        if (f2) { fclose(f2); return p2; }
        return p1;
    };
    if (G->verbose) {
        fprintf(stderr, "[java] call %s(", name.c_str());
        for (size_t i = 0; i < a.size(); i++) {
            JObj* o = obj((u32)a[i]);
            if (o && o->k == 2) fprintf(stderr, "\"%s\"%s", o->s.c_str(), i + 1 < a.size() ? ", " : "");
            else fprintf(stderr, "0x%llx%s", (unsigned long long)a[i], i + 1 < a.size() ? ", " : "");
        }
        fprintf(stderr, ")\n");
    }
    if (name == "getAssetSize") {
        JObj* s = obj((u32)a[0]); if (!s) return (u32)-1;
        FILE* f = fopen(asset(s->s).c_str(), "rb"); if (!f) return (u32)-1;
        fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f); return (u32)n;
    }
    if (name == "loadAsset") {
        int id = (int)a[0]; JObj* b = obj((u32)a[1]); int len = (int)a[2];
        if (id < 0 || id >= (int)g_assetNames.size() || !b) return (u32)-1;
        FILE* f = fopen(asset(g_assetNames[id]).c_str(), "rb"); if (!f) return (u32)-1;
        if ((int)b->b.size() < len) b->b.resize(len);
        size_t n = fread(b->b.data(), 1, len, f); fclose(f); return (u32)n;
    }
    if (name == "getUniqueNumberFromStrings") {
        JObj* s = obj((u32)a[0]); if (!s) return (u32)-1;
        auto it = g_assetIds.find(s->s);
        if (it != g_assetIds.end()) return it->second;
        int id = (int)g_assetNames.size(); g_assetIds[s->s] = id; g_assetNames.push_back(s->s); return id;
    }
    if (name == "getUniqueAssetNameFromID") {
        int id = (int)a[0]; if (id < 0 || id >= (int)g_assetNames.size()) return 0; return stringObj(g_assetNames[id]);
    }
    if (name == "getAvailableInternalMemorySize" || name == "getAvailableExternalMemorySize") { hi = 0; return 0x40000000; }
    if (name == "getTotalInternalMemorySize" || name == "getTotalExternalMemorySize") { hi = 0; return 0x7FFFFFFF; }
    if (name == "getOSVersion") return 10;
    if (name == "getOpenGLESVersion") return stringObj("OpenGL ES-CM 1.1");
    if (name == "getManufacturer") return stringObj("samsung");
    if (name == "getModel") return stringObj("SHM-M250S");
    if (name == "getIMEI") return stringObj("000000000000000");
    if (name == "getIMSI") return stringObj("450000000000000");
    if (name == "getMin") return stringObj("00000000000");
    if (name == "totalMemory" || name == "freeMemory") return 0x10000000;
    if (name == "putMusicClip" || name == "putSoundClip") {
        if(a.size()<3) return (u32)-1;
        return Audio::load(vfsPath(),a[0],a[1],a[2],name=="putMusicClip");
    }
    if (name.rfind("clipPlay",0)==0) return a.size()>1?Audio::play(a[0],a[1]!=0):(u32)-1;
    if (name.rfind("clipStop",0)==0) return a.empty()?(u32)-1:Audio::stop(a[0]);
    if (name.rfind("clipFree",0)==0) return a.empty()?(u32)-1:Audio::free(a[0]);
    if (name.rfind("clipPause",0)==0) return a.empty()?(u32)-1:Audio::pause(a[0]);
    if (name.rfind("clipResume",0)==0) return a.empty()?(u32)-1:Audio::resume(a[0]);
    if (name=="setMusicVolume" || name=="setSoundVolume" || name=="setVolume") {
        if(!a.empty()) { Audio::volume(name=="setMusicVolume",(int)a[0]); if(name=="setVolume") Audio::volume(true,(int)a[0]); }
        return 0;
    }
    if (name == "getSoundVolume" || name == "getMusicVolume" || name == "getVolume") return Audio::volume(name=="getMusicVolume");
    if (name == "initMTX" || name == "requestMTX" || name == "phnCallPlace") return (u32)-1;
    if (name == "exitApp") { G->quit = true; return 0; }
    if (name.rfind("clip", 0) == 0 || name == "putSoundClip" || name == "putMusicClip" || name == "putClip" || name == "getClipStatus" ||
        name == "setSoundVolume" || name == "setMusicVolume" || name == "setVolume" || name == "vibration" ||
        name == "isFBOSupported" || name == "isNetworkAvailable" || name == "isAirplaneModeOn" || name == "isNetworkRoaming" ||
        name == "attachBufferIDtoResourceID" || name == "detachBufferIDfromResourceID")
        return 0;
    static std::map<std::string, int> warned;
    if (warned[name]++ == 0) fprintf(stderr, "[java] unimplemented static %s%s\n", name.c_str(), sig.c_str());
    return 0;
}


static std::vector<std::function<void(Cpu&)>> g_jni(235);
static u32 g_env = 0, g_vm = 0;

static void jniReturnByType(Cpu& c, char t, u32 lo, u32 hi) {
    c.r[0] = lo;
    if (t == 'J' || t == 'D') c.r[1] = hi;
}

static void doStaticCall(Cpu& c, bool isA) {
    u32 mid = c.r[2], va = c.r[3];
    if (mid < 0x1000 || mid - 0x1000 >= g_methods.size()) { fprintf(stderr, "[JNI] bad methodID\n"); abort(); }
    const JM& m = g_methods[mid - 0x1000];
    std::vector<u64> args;
    ArgReader ar; ar.ap = va;
    size_t i = 1;
    char ret = 'V';
    const std::string& s = m.sig;
    std::vector<char> types;
    while (i < s.size() && s[i] != ')') {
        char t = s[i];
        if (t == 'L') { while (s[i] != ';') i++; types.push_back('L'); }
        else if (t == '[') { while (s[i] == '[') i++; if (s[i] == 'L') while (s[i] != ';') i++; types.push_back('L'); }
        else types.push_back(t);
        i++;
    }
    ret = s[i + 1];
    if (ret == '[') ret = 'L';
    for (char t : types) {
        u64 v;
        if (isA) { v = (u64)gm::r32(va) | ((u64)gm::r32(va + 4) << 32); va += 8; if (t != 'J' && t != 'D') v &= 0xFFFFFFFFu; }
        else if (t == 'J' || t == 'D') v = ar.n64();
        else if (t == 'F') { double d; u64 raw = ar.n64(); memcpy(&d, &raw, 8); float f = (float)d; u32 b; memcpy(&b, &f, 4); v = b; }
        else v = ar.n32();
        args.push_back(v);
    }
    u32 hi = 0;
    u32 lo = javaStatic(m.name, m.sig, args, hi);
    if (G->verbose) fprintf(stderr, "[java] %s%s -> %u\n", m.name.c_str(), m.sig.c_str(), lo);
    jniReturnByType(c, ret, lo, hi);
}

static void setupJni(Cpu& cpu) {
    u32 table = gm::malloc_(235 * 4);
    for (int i = 0; i < 235; i++) {
        u32 stub = cpu.addTrap("jni#" + std::to_string(i), [i](Cpu& c) {
            if (!g_jni[i]) { fprintf(stderr, "[JNI] unimplemented function index %d (lr=0x%08x)\n", i, c.r[14]); fflush(stderr); abort(); }
            g_jni[i](c);
        });
        gm::w32(table + 4 * i, stub);
    }
    g_env = gm::malloc_(4); gm::w32(g_env, table);
    u32 vt = gm::malloc_(32);
    g_vm = gm::malloc_(4); gm::w32(g_vm, vt);
    for (int i = 0; i < 8; i++)
        gm::w32(vt + 4 * i, cpu.addTrap("jvm#" + std::to_string(i), [](Cpu& c) { gm::w32(c.r[1], g_env); c.r[0] = 0; }));

    g_jni[4] = [](Cpu& c) { c.r[0] = 0x10006; };                                     // GetVersion
    g_jni[6] = [](Cpu& c) {                                                          // FindClass
        std::string n = gm::cstr(c.r[1]);
        auto it = g_classes.find(n);
        if (it == g_classes.end()) { JObj o; o.k = 1; o.s = n; it = g_classes.emplace(n, newObj(std::move(o))).first; }
        c.r[0] = it->second; };
    g_jni[21] = [](Cpu& c) { c.r[0] = c.r[1]; };                                     // NewGlobalRef
    g_jni[22] = [](Cpu& c) { c.r[0] = 0; };                                          // DeleteGlobalRef
    g_jni[23] = [](Cpu& c) {                                                         // DeleteLocalRef
        JObj* o = obj(c.r[1]);
        if (o && (o->k == 2 || o->k == 3)) { o->k = 0; o->s.clear(); o->b.clear(); g_objFree.push_back(c.r[1] - 0x100); }
        c.r[0] = 0; };
    g_jni[25] = [](Cpu& c) { c.r[0] = c.r[1]; };                                     // NewLocalRef
    g_jni[26] = [](Cpu& c) { c.r[0] = 0; };                                          // EnsureLocalCapacity
    g_jni[15] = [](Cpu& c) { c.r[0] = 0; };                                          // ExceptionOccurred
    g_jni[16] = [](Cpu& c) { c.r[0] = 0; };                                          // ExceptionDescribe
    g_jni[17] = [](Cpu& c) { c.r[0] = 0; };                                          // ExceptionClear
    g_jni[31] = [](Cpu& c) { JObj* o = obj(c.r[1]); std::string n = o ? o->s : ""; auto it = g_classes.find("co/kr/eamobile/CletEntry"); c.r[0] = it != g_classes.end() ? it->second : 0; (void)n; };
    auto mid = [](Cpu& c) {                                                          // Get(Static)MethodID / GetFieldID
        JObj* k = obj(c.r[1]);
        g_methods.push_back({k ? k->s : "", gm::cstr(c.r[2]), gm::cstr(c.r[3])});
        c.r[0] = 0x1000 + (u32)g_methods.size() - 1; };
    g_jni[33] = mid; g_jni[94] = mid; g_jni[113] = mid; g_jni[144] = mid;
    g_jni[100] = [](Cpu& c) {
        JObj* o = obj(c.r[1]);
        c.r[0] = (o && o->k == 4) ? VFS_FAKE_FD : 0;
        if (G->verbose) fprintf(stderr, "[JNI GetIntField] obj=0x%x k=%d fieldID=0x%x -> %u\n", c.r[1], o ? o->k : -1, c.r[2], c.r[0]);
    };
    for (int g = 0; g < 10; g++) { g_jni[114 + 3 * g + 1] = [](Cpu& c) { doStaticCall(c, false); }; g_jni[114 + 3 * g + 2] = [](Cpu& c) { doStaticCall(c, true); }; }
    g_jni[164] = [](Cpu& c) { JObj* o = obj(c.r[1]); c.r[0] = o ? (u32)o->s.size() : 0; };      // GetStringLength (ascii)
    g_jni[167] = [](Cpu& c) { c.r[0] = stringObj(gm::cstr(c.r[1])); };                          // NewStringUTF
    g_jni[168] = [](Cpu& c) { JObj* o = obj(c.r[1]); c.r[0] = o ? (u32)o->s.size() : 0; };      // GetStringUTFLength
    g_jni[169] = [](Cpu& c) {                                                                    // GetStringUTFChars
        JObj* o = obj(c.r[1]); if (c.r[2]) gm::w8(c.r[2], 1);
        c.r[0] = gm::put_cstr(o ? o->s : ""); };
    g_jni[170] = [](Cpu& c) { gm::free_(c.r[2]); c.r[0] = 0; };                                  // ReleaseStringUTFChars
    g_jni[171] = [](Cpu& c) { JObj* o = obj(c.r[1]); c.r[0] = o ? (u32)o->b.size() : 0; };       // GetArrayLength
    g_jni[176] = [](Cpu& c) { JObj o; o.k = 3; o.b.assign(c.r[1], 0); c.r[0] = newObj(std::move(o)); };  // NewByteArray
    g_jni[184] = [](Cpu& c) {                                                                    // GetByteArrayElements
        JObj* o = obj(c.r[1]); u32 n = o ? (u32)o->b.size() : 0; u32 p = gm::malloc_(n + 1);
        if (n) memcpy(gm::ptr(p, n), o->b.data(), n);
        if (c.r[2]) { gm::w8(c.r[2], 1); }
        c.r[0] = p;
    };
    g_jni[192] = [](Cpu& c) {                                                                    // ReleaseByteArrayElements(env, arr, elems, mode)
        JObj* o = obj(c.r[1]);
        if (o && c.r[3] != 2 && !o->b.empty()) memcpy(o->b.data(), gm::ptr(c.r[2], (u32)o->b.size()), o->b.size());
        if (c.r[3] != 1) { gm::free_(c.r[2]); }
        c.r[0] = 0;
    };
    g_jni[200] = [](Cpu& c) {                                                                    // GetByteArrayRegion(env, arr, start, len, buf)
        JObj* o = obj(c.r[1]); u32 buf = c.arg(4);
        if (o && c.r[3]) { if (c.r[2] + c.r[3] > o->b.size()) o->b.resize(c.r[2] + c.r[3]); memcpy(gm::ptr(buf, c.r[3]), o->b.data() + c.r[2], c.r[3]); }
        c.r[0] = 0; };
    g_jni[208] = [](Cpu& c) {                                                                    // SetByteArrayRegion
        JObj* o = obj(c.r[1]); u32 buf = c.arg(4);
        if (o && c.r[3]) { if (c.r[2] + c.r[3] > o->b.size()) o->b.resize(c.r[2] + c.r[3]); memcpy(o->b.data() + c.r[2], gm::ptr(buf, c.r[3]), c.r[3]); }
        c.r[0] = 0; };
    g_jni[217] = [](Cpu& c) { c.r[0] = 0; };                                                     // MonitorEnter
    g_jni[218] = [](Cpu& c) { c.r[0] = 0; };                                                     // MonitorExit
    g_jni[219] = [](Cpu& c) { gm::w32(c.r[1], g_vm); c.r[0] = 0; };                              // GetJavaVM
}

// ---------------------------------------------------------------- libc / libm / GLES registry
static std::map<std::string, u32> g_importAddr;
static std::map<std::string, std::function<void(Cpu&)>> g_lib;

static u32 hostFdOpen(const std::string& path, u32 flags) {
    int wf = 0, acc = flags & 3;
    wf |= (acc == 0 ? O_RDONLY : acc == 1 ? O_WRONLY : O_RDWR);
    if (flags & 0x40) wf |= O_CREAT;
    if (flags & 0x200) wf |= O_TRUNC;
    if (flags & 0x400) wf |= O_APPEND;
#ifdef _WIN32
    return (u32)_open(path.c_str(), wf | O_BINARY, _S_IREAD | _S_IWRITE);
#else
    return (u32)open(path.c_str(), wf | O_BINARY, 0666);
#endif
}

static void setupLib() {
    auto& L = g_lib;
    // ---- memory / strings
    L["malloc"] = [](Cpu& c) { c.r[0] = gm::malloc_(c.r[0]); };
    L["calloc"] = [](Cpu& c) { u32 n = c.r[0] * c.r[1]; u32 p = gm::malloc_(n); memset(gm::ptr(p, n ? n : 1), 0, n); c.r[0] = p; };
    L["free"] = [](Cpu& c) { gm::free_(c.r[0]); };
    L["memcpy"] = [](Cpu& c) { if (c.r[2]) memcpy(gm::ptr(c.r[0], c.r[2]), gm::ptr(c.r[1], c.r[2]), c.r[2]); };
    L["memmove"] = [](Cpu& c) { if (c.r[2]) memmove(gm::ptr(c.r[0], c.r[2]), gm::ptr(c.r[1], c.r[2]), c.r[2]); };
    L["memset"] = [](Cpu& c) { if (c.r[2]) memset(gm::ptr(c.r[0], c.r[2]), (int)c.r[1], c.r[2]); };
    L["strlen"] = [](Cpu& c) { c.r[0] = (u32)gm::cstr(c.r[0]).size(); };
    L["strcpy"] = [](Cpu& c) { std::string s = gm::cstr(c.r[1]); memcpy(gm::ptr(c.r[0], (u32)s.size() + 1), s.c_str(), s.size() + 1); };
    L["strncpy"] = [](Cpu& c) { std::string s = gm::cstr(c.r[1]); u32 n = c.r[2]; if (!n) return; u8* d = gm::ptr(c.r[0], n); size_t k = std::min<size_t>(s.size(), n); memcpy(d, s.data(), k); if (k < n) memset(d + k, 0, n - k); };
    L["strcat"] = [](Cpu& c) { std::string a = gm::cstr(c.r[0]), b = gm::cstr(c.r[1]); memcpy(gm::ptr(c.r[0] + (u32)a.size(), (u32)b.size() + 1), b.c_str(), b.size() + 1); };
    L["strcmp"] = [](Cpu& c) { int v = strcmp(gm::cstr(c.r[0]).c_str(), gm::cstr(c.r[1]).c_str()); c.r[0] = (u32)(v < 0 ? -1 : v > 0 ? 1 : 0); };
    L["strncmp"] = [](Cpu& c) { int v = strncmp(gm::cstr(c.r[0]).c_str(), gm::cstr(c.r[1]).c_str(), c.r[2]); c.r[0] = (u32)(v < 0 ? -1 : v > 0 ? 1 : 0); };
    L["strstr"] = [](Cpu& c) { std::string h = gm::cstr(c.r[0]), n = gm::cstr(c.r[1]); size_t p = h.find(n); c.r[0] = p == std::string::npos ? 0 : c.r[0] + (u32)p; };
    L["strdup"] = [](Cpu& c) { c.r[0] = gm::put_cstr(gm::cstr(c.r[0])); };
    L["strerror"] = [](Cpu& c) { static u32 p = 0; if (!p) p = gm::put_cstr("error"); c.r[0] = p; };
    L["qsort"] = [](Cpu& c) {
        u32 base = c.r[0], n = c.r[1], sz = c.r[2], cmp = c.r[3];
        std::vector<u32> idx(n);
        for (u32 i = 0; i < n; i++) idx[i] = i;
        std::vector<u8> copy(gm::ptr(base, n * sz), gm::ptr(base, n * sz) + n * sz);
        u32 tmpA = gm::malloc_(sz), tmpB = gm::malloc_(sz);
        std::stable_sort(idx.begin(), idx.end(), [&](u32 a, u32 b) {
            memcpy(gm::ptr(tmpA, sz), &copy[a * sz], sz); memcpy(gm::ptr(tmpB, sz), &copy[b * sz], sz);
            return (int)c.call(cmp, {tmpA, tmpB}) < 0; });
        for (u32 i = 0; i < n; i++) memcpy(gm::ptr(base + i * sz, sz), &copy[idx[i] * sz], sz);
        gm::free_(tmpA); gm::free_(tmpB); };
    L["vsnprintf"] = [](Cpu& c) {
        ArgReader a; a.ap = c.r[3];
        std::string s = guestFormat(gm::cstr(c.r[2]), a);
        u32 n = c.r[1]; if (n) { size_t k = std::min<size_t>(s.size(), n - 1); memcpy(gm::ptr(c.r[0], (u32)k + 1), s.c_str(), k); gm::w8(c.r[0] + (u32)k, 0); }
        c.r[0] = (u32)s.size(); };
    L["__android_log_print"] = [](Cpu& c) {
        ArgReader a; a.cpu = &c; a.regIdx = 3;
        std::string s = guestFormat(gm::cstr(c.r[2]), a);
        if (G->verbose) fprintf(stderr, "[guest:%s] %s\n", gm::cstr(c.r[1]).c_str(), s.c_str());
        c.r[0] = 0; };
    // ---- libm (soft-float ABI: doubles in r0:r1 / r2:r3)
    L["floor"] = [](Cpu& c) { retd(c, floor(bd(c.r[0], c.r[1]))); };
    L["ceil"] = [](Cpu& c) { retd(c, ceil(bd(c.r[0], c.r[1]))); };
    L["pow"] = [](Cpu& c) { retd(c, pow(bd(c.r[0], c.r[1]), bd(c.r[2], c.r[3]))); };
    // ---- threads / misc
    L["pthread_mutex_lock"] = [](Cpu& c) { c.r[0] = 0; };
    L["pthread_mutex_unlock"] = [](Cpu& c) { c.r[0] = 0; };
    L["__errno"] = [](Cpu& c) { c.r[0] = g_errnoAddr; };
    L["abort"] = [](Cpu& c) { fprintf(stderr, "[guest] abort() lr=0x%08x\n", c.r[14]); fflush(stderr); ::abort(); };
    L["__stack_chk_fail"] = [](Cpu& c) { fprintf(stderr, "[guest] stack smashing detected lr=0x%08x\n", c.r[14]); fflush(stderr); ::abort(); };
    L["ftime"] = [](Cpu& c) {
        gm::w32(c.r[0], (u32)(g_simTimeMs / 1000));
        gm::w16(c.r[0] + 4, (u16)(g_simTimeMs % 1000));
        gm::w16(c.r[0] + 6, 0);
        gm::w16(c.r[0] + 8, 0);
        c.r[0] = 0;
    };
    for (const char* n : {"__gnu_Unwind_Find_exidx", "__cxa_begin_cleanup", "__cxa_call_unexpected", "__cxa_type_match"})
        L[n] = [n](Cpu& c) { fprintf(stderr, "[guest] C++ exception machinery called (%s) lr=0x%08x\n", n, c.r[14]); fflush(stderr); ::abort(); };
    // ---- sockets: networking is not emulated (offline), calls fail like "no network"
    for (const char* n : {"socket", "connect", "recv", "select", "setsockopt", "getsockopt", "shutdown", "fcntl"})
        L[n] = [](Cpu& c) { c.r[0] = (u32)-1; };
    L["inet_addr"] = [](Cpu& c) { c.r[0] = 0xFFFFFFFFu; };
    // ---- file system
    L["open"] = [](Cpu& c) { c.r[0] = hostFdOpen(gm::cstr(c.r[0]), c.r[1]); };
    L["close"] = [](Cpu& c) {
        if (c.r[0] >= (u32)VFS_FAKE_FD) { c.r[0] = 0; return; }
#ifdef _WIN32
        c.r[0] = (u32)_close((int)c.r[0]);
#else
        c.r[0] = (u32)close((int)c.r[0]);
#endif
    };
    L["read"] = [](Cpu& c) {
#ifdef _WIN32
        c.r[0] = (u32)_read((int)c.r[0], gm::ptr(c.r[1], c.r[2] ? c.r[2] : 1), c.r[2]);
#else
        c.r[0] = (u32)read((int)c.r[0], gm::ptr(c.r[1], c.r[2] ? c.r[2] : 1), c.r[2]);
#endif
    };
    L["write"] = [](Cpu& c) {
#ifdef _WIN32
        c.r[0] = (u32)_write((int)c.r[0], gm::ptr(c.r[1], c.r[2] ? c.r[2] : 1), c.r[2]);
#else
        c.r[0] = (u32)write((int)c.r[0], gm::ptr(c.r[1], c.r[2] ? c.r[2] : 1), c.r[2]);
#endif
    };
    L["lseek"] = [](Cpu& c) {
#ifdef _WIN32
        c.r[0] = (u32)_lseek((int)c.r[0], (long)c.r[1], (int)c.r[2]);
#else
        c.r[0] = (u32)lseek((int)c.r[0], (off_t)c.r[1], (int)c.r[2]);
#endif
    };
    L["dup"] = [](Cpu& c) {
        u32 old = c.r[0];
#ifdef _WIN32
        c.r[0] = c.r[0] >= (u32)VFS_FAKE_FD ? c.r[0] + 1 : (u32)_dup((int)c.r[0]);
#else
        c.r[0] = c.r[0] >= (u32)VFS_FAKE_FD ? c.r[0] + 1 : (u32)dup((int)c.r[0]);
#endif
        if (G->verbose) fprintf(stderr, "[vfs] dup(fd=%u) -> %u\n", old, c.r[0]);
    };
    L["access"] = [](Cpu& c) {
#ifdef _WIN32
        c.r[0] = (u32)_access(gm::cstr(c.r[0]).c_str(), (int)(c.r[1] & 6));
#else
        c.r[0] = (u32)access(gm::cstr(c.r[0]).c_str(), (int)(c.r[1] & 6));
#endif
    };
    L["mkdir"] = [](Cpu& c) {
#ifdef _WIN32
        c.r[0] = (u32)_mkdir(gm::cstr(c.r[0]).c_str());
#else
        c.r[0] = (u32)mkdir(gm::cstr(c.r[0]).c_str(), 0755);
#endif
    };
    L["rmdir"] = [](Cpu& c) {
#ifdef _WIN32
        c.r[0] = (u32)_rmdir(gm::cstr(c.r[0]).c_str());
#else
        c.r[0] = (u32)rmdir(gm::cstr(c.r[0]).c_str());
#endif
    };
    L["unlink"] = [](Cpu& c) { c.r[0] = (u32)remove(gm::cstr(c.r[0]).c_str()); };
    L["rename"] = [](Cpu& c) { c.r[0] = (u32)rename(gm::cstr(c.r[0]).c_str(), gm::cstr(c.r[1]).c_str()); };
    L["chdir"] = [](Cpu& c) { c.r[0] = 0; };
    L["stat"] = [](Cpu& c) {
        std::string path = gm::cstr(c.r[0]);
#ifdef _WIN32
        struct _stat64i32 st;
        if (_stat64i32(path.c_str(), &st) != 0) { c.r[0] = (u32)-1; return; }
        u32 mode = (st.st_mode & _S_IFDIR) ? 040755 : 0100644;
#else
        struct stat st;
        if (stat(path.c_str(), &st) != 0) { c.r[0] = (u32)-1; return; }
        u32 mode = S_ISDIR(st.st_mode) ? 040755 : 0100644;
#endif
        u8* b = gm::ptr(c.r[1], 104); memset(b, 0, 104);
        gm::w32(c.r[1] + 16, mode); gm::w32(c.r[1] + 48, (u32)st.st_size); gm::w32(c.r[1] + 52, 0);
        gm::w32(c.r[1] + 72, (u32)st.st_atime); gm::w32(c.r[1] + 80, (u32)st.st_mtime); gm::w32(c.r[1] + 88, (u32)st.st_ctime);
        c.r[0] = 0;
    };
    L["fdopen"] = [](Cpu& c) {
        u32 guestFd = c.r[0];
        if (guestFd < (u32)VFS_FAKE_FD) { c.r[0] = 0; return; }
        FILE* f = fopen(vfsPath().c_str(), "rb");
        u32 h = gm::malloc_(16); g_files[h] = f; c.r[0] = f ? h : 0;
        if (G->verbose) fprintf(stderr, "[vfs] fdopen(guestFd=%u) -> h=0x%x f=%p (vfsPath=%s)\n", guestFd, c.r[0], f, vfsPath().c_str());
    };
    L["fclose"] = [](Cpu& c) {
        auto it = g_files.find(c.r[0]);
        if (it != g_files.end()) {
            if (G->verbose) fprintf(stderr, "[vfs] fclose(h=0x%x)\n", c.r[0]);
            fclose(it->second); g_files.erase(it); gm::free_(c.r[0]);
        }
        c.r[0] = 0;
    };
    L["fread"] = [](Cpu& c) {
        auto it = g_files.find(c.r[3]); if (it == g_files.end()) { c.r[0] = 0; return; }
        u32 tot = c.r[1] * c.r[2]; c.r[0] = tot ? (u32)(fread(gm::ptr(c.r[0], tot), c.r[1], c.r[2], it->second)) : 0; };
    L["fseek"] = [](Cpu& c) { auto it = g_files.find(c.r[0]); c.r[0] = it == g_files.end() ? (u32)-1 : (u32)fseek(it->second, (long)c.r[1], (int)c.r[2]); };
    // ---- GLES 1.x (software compositor)
    for (const char* n : {"glEnable", "glDisable", "glEnableClientState", "glDisableClientState", "glMatrixMode", "glLoadIdentity",
                          "glTexParameterf", "glCullFace", "glColor4f", "glViewport"})
        L[n] = [](Cpu& c) { c.r[0] = 0; };
    L["glOrthof"] = [](Cpu& c) {
        g_ortho[0] = bf(c.r[0]); g_ortho[1] = bf(c.r[1]); g_ortho[2] = bf(c.r[2]); g_ortho[3] = bf(c.r[3]);
        fprintf(stderr, "[GL] glOrthof %f %f %f %f\n", g_ortho[0], g_ortho[1], g_ortho[2], g_ortho[3]);
        int w = (int)(fabsf(g_ortho[1] - g_ortho[0]) + 0.5f), h = (int)(fabsf(g_ortho[2] - g_ortho[3]) + 0.5f);
        if (w > 0 && h > 0 && (w != G->screen.w || h != G->screen.h)) { G->screen.w = w; G->screen.h = h; G->screen.px.assign((size_t)w * h, 0xFF000000u); } };
    L["glGenTextures"] = [](Cpu& c) { for (u32 i = 0; i < c.r[0]; i++) { gm::w32(c.r[1] + 4 * i, g_nextTex); if (G->verbose) fprintf(stderr, "[GL] genTex -> %u\n", g_nextTex); g_nextTex++; } };
    L["glDeleteTextures"] = [](Cpu& c) { for (u32 i = 0; i < c.r[0]; i++) g_tex.erase(gm::r32(c.r[1] + 4 * i)); };
    L["glBindTexture"] = [](Cpu& c) { g_bound = c.r[1]; };
    L["glVertexPointer"] = [](Cpu& c) { g_vtxSize = c.r[0]; g_vtxType = c.r[1]; g_vtxStride = c.r[2]; g_vtxPtr = c.r[3]; };
    L["glTexCoordPointer"] = [](Cpu& c) { g_tcSize = c.r[0]; g_tcType = c.r[1]; g_tcStride = c.r[2]; g_tcPtr = c.r[3]; };
    L["glTexImage2D"] = [](Cpu& c) {
        Tex& t = g_tex[g_bound]; t.w = (int)c.r[3]; t.h = (int)c.arg(4); t.fmt = c.arg(6); t.type = c.arg(7);
        int bpp = bppOf(t.fmt, t.type); t.d.assign((size_t)t.w * t.h * bpp, 0);
        u32 px = c.arg(8);
        if (G->verbose) fprintf(stderr, "[GL] glTexImage2D tex=%u w=%d h=%d fmt=0x%x type=0x%x bpp=%d px=0x%x\n", g_bound, t.w, t.h, t.fmt, t.type, bpp, px);
        if (px) memcpy(t.d.data(), gm::ptr(px, (u32)t.d.size()), t.d.size()); };
    L["glTexSubImage2D"] = [](Cpu& c) {
        Tex& t = g_tex[g_bound]; int xo = (int)c.r[2], yo = (int)c.r[3], w = (int)c.arg(4), h = (int)c.arg(5); u32 fmt = c.arg(6), type = c.arg(7), px = c.arg(8);
        int bpp = bppOf(t.fmt ? t.fmt : fmt, t.type ? t.type : type);
        if (G->verbose) fprintf(stderr, "[GL] glTexSubImage2D tex=%u xo=%d yo=%d w=%d h=%d fmt=0x%x type=0x%x px=0x%x (tex w=%d h=%d)\n", g_bound, xo, yo, w, h, fmt, type, px, t.w, t.h);
        for (int y = 0; y < h && px; y++) {
            if (yo + y >= t.h) break;
            memcpy(&t.d[((size_t)(yo + y) * t.w + xo) * bpp], gm::ptr(px + (u32)(y * w * bpp), w * bpp), (size_t)w * bpp);
        } };
    L["glDrawArrays"] = [](Cpu& c) {
        if (G->verbose) fprintf(stderr, "[GL] glDrawArrays mode=%u first=%u count=%u (bound=%u)\n", c.r[0], c.r[1], c.r[2], g_bound);
        glDraw(c.r[0], c.r[1], c.r[2]); };
    L["__stack_chk_guard"] = [](Cpu&) {};
}

static u32 resolveImport(Cpu& cpu, const std::string& name) {
    auto it = g_importAddr.find(name);
    if (it != g_importAddr.end()) return it->second;
    u32 a;
    if (name == "__stack_chk_guard") a = g_stackGuard;
    else {
        auto f = g_lib.find(name);
        if (f != g_lib.end()) a = cpu.addTrap(name, f->second);
        else a = cpu.addTrap(name, [name](Cpu& c) { fprintf(stderr, "[HLE] unimplemented import '%s' called (lr=0x%08x)\n", name.c_str(), c.r[14]); fflush(stderr); ::abort(); });
    }
    g_importAddr[name] = a;
    return a;
}

// ---------------------------------------------------------------- Game
static u32 g_thiz = 0;
static u32 sym(const char* n) {
    auto it = G->img.exports.find(n);
    if (it == G->img.exports.end()) { fprintf(stderr, "[boot] missing export %s\n", n); abort(); }
    return it->second;
}

bool Game::boot(const std::string& apkDir, const std::string& dataDir, int lcdW, int lcdH) {
    if (apkDir.rfind("romfs:", 0) == 0 || apkDir.rfind("sdmc:", 0) == 0) {
        g_apkDir = apkDir;
    } else {
        std::error_code ec;
        g_apkDir = std::filesystem::absolute(apkDir, ec).generic_string();
        if (g_apkDir.empty()) g_apkDir = apkDir;
    }

    if (dataDir.rfind("romfs:", 0) == 0 || dataDir.rfind("sdmc:", 0) == 0) {
        g_dataDir = dataDir;
    } else {
        std::error_code ec;
        g_dataDir = std::filesystem::absolute(dataDir, ec).generic_string();
        if (g_dataDir.empty()) g_dataDir = dataDir;
    }
    G = this;
    LOGI("[boot] absApk=%s absData=%s\n", g_apkDir.c_str(), g_dataDir.c_str());
    gm::init();
    gm::registerCpuDumper([]() {
        if (G) {
            fprintf(stderr, "[CPU DUMP] PC=0x%08x LR=0x%08x icount=%llu\n", G->cpu.r[15], G->cpu.r[14], (unsigned long long)G->cpu.icount);
            for (int i = 0; i < 16; i++) {
                fprintf(stderr, " r%d=0x%08x", i, G->cpu.r[i]);
                if ((i & 3) == 3) fprintf(stderr, "\n");
            }
        }
    });
    cpu.r[13] = gm::STACK_TOP;
    g_errnoAddr = gm::malloc_(16);
    g_stackGuard = gm::malloc_(16); gm::w32(g_stackGuard, 0xA5C3B2D1);
    setupLib();
    setupJni(cpu);
    Trans::init(dataDir + "/dict.bin");
    std::error_code ec;
    std::filesystem::create_directories(dataDir + "/internal", ec);
    std::filesystem::create_directories(dataDir + "/external", ec);

    std::string sep = (g_apkDir.empty() || g_apkDir.back() == '/') ? "" : "/";
    std::string testPath = "assets_override/c/sp/imgcom/title.mgr";
    FILE* testF = fopen(testPath.c_str(), "rb");
    if (testF) {
        fclose(testF);
        g_overrideDir = "assets_override";
    } else {
        testPath = g_apkDir + sep + "assets_override/c/sp/imgcom/title.mgr";
        testF = fopen(testPath.c_str(), "rb");
        if (testF) {
            fclose(testF);
            g_overrideDir = g_apkDir + sep + "assets_override";
        } else {
            testPath = "../assets_override/c/sp/imgcom/title.mgr";
            testF = fopen(testPath.c_str(), "rb");
            if (testF) {
                fclose(testF);
                g_overrideDir = "../assets_override";
            }
        }
    }

    std::string soPath = apkDir + sep + "libHeroesLore5.so";
    FILE* soF = fopen(soPath.c_str(), "rb");
    if (!soF) {
        soPath = apkDir + sep + "lib/armeabi/libHeroesLore5.so";
        soF = fopen(soPath.c_str(), "rb");
    }
    if (soF) fclose(soF);
    if (!elf_load(soPath, img, [this](const std::string& n) { return resolveImport(cpu, n); })) {
        LOGE("[boot] Failed to load ELF: %s\n", soPath.c_str());
        return false;
    }
    LOGI("[boot] ELF loaded successfully: %s\n", soPath.c_str());
    for (u32 f : img.initArray) { if (f && f != 0xFFFFFFFFu) cpu.call(f, {}); }

    { JObj o; o.k = 5; g_thiz = newObj(std::move(o)); }
    cpu.call(sym("Java_co_kr_eamobile_CletEntry_nativeInitKernel"), {g_env, g_thiz});
    // nativeInitDisplay(screenW, screenH, r,g,b,a, wipiW, wipiH, marginX, marginY, colorType, mag(F), filter, viewMode, subLayer, subPixel)
    float mag = 1.0f; u32 magBits; memcpy(&magBits, &mag, 4);
    cpu.callv(sym("Java_co_kr_eamobile_CletEntry_nativeInitDisplay"),
              {g_env, g_thiz, (u32)lcdW, (u32)lcdH, 5, 6, 5, 0, (u32)lcdW, (u32)lcdH, 0, 0, 0, magBits, 0, 0, 0, 0});
    std::string in = dataDir + "/internal", ex = dataDir + "/external";
    cpu.call(sym("Java_co_kr_eamobile_CletEntry_nativeInitFileStorage"), {g_env, g_thiz, stringObj(in), stringObj(ex)});
    {   // VFS: FileDescriptor object, long offset, long length (AAPCS: obj in r2, pad r3, then 64-bit values on stack)
        FILE* f = fopen(vfsPath().c_str(), "rb"); if (!f) { LOGE("[boot] missing %s\n", vfsPath().c_str()); return false; }
        fseek(f, 0, SEEK_END); g_vfsLen = (u64)ftell(f); fclose(f);
        JObj fd; fd.k = 4; u32 fdh = newObj(std::move(fd));
        LOGI("[boot] calling nativeInitVFS fdh=%u len=%llu...\n", fdh, (unsigned long long)g_vfsLen);
        cpu.callv(sym("Java_co_kr_eamobile_CletEntry_nativeInitVFS"), {g_env, g_thiz, fdh, 0, 0, 0, (u32)g_vfsLen, (u32)(g_vfsLen >> 32)});
        LOGI("[boot] nativeInitVFS done.\n");
    }
    cpu.callv(sym("Java_co_kr_eamobile_CletEntry_nativeInitAudioConfiguration"), {g_env, g_thiz, 0, 0});
    cpu.call(sym("Java_co_kr_eamobile_CletEntry_nativeStartApp"), {g_env, g_thiz});
    LOGI("[boot] game started (icount=%llu)\n", (unsigned long long)cpu.icount);
    return true;
}

static bool g_viewportRepaint=false;
static unsigned g_viewportGeneration=0;
static u32 g_lastPaintedMap=0;
static int g_previousViewportW=240,g_previousViewportH=400;

void refreshViewportDialog(Cpu& cpu) {
    if(!g_viewportGeneration) return;
    u32 event=cpu.r[0];
    int index=(int8_t)gm::r8(event+0xac);
    if(index<0 || index>3) return;
    u32 record=event+(u32)index*60;
    int boxHeight=gm::r16(record+0xca);
    if(boxHeight<10 || boxHeight>G->screen.h/2) return;
    if(gm::r16(record+0xc8)==G->screen.w && gm::r16(record+0xc6)+boxHeight==G->screen.h) return;
    // Reflow the existing message; keep its text, typing progress and script state.
    gm::w16(record+0xc4,0);gm::w16(record+0xc6,G->screen.h-boxHeight);
    gm::w16(record+0xc8,G->screen.w);
    gm::w16(record+0xbe,8);gm::w16(record+0xc0,G->screen.h-boxHeight+6);
    gm::w16(record+0xc2,G->screen.w-16);
}

void refreshViewportMap(Cpu& cpu) {
    u32 map=cpu.r[0];
    bool sameMap=g_lastPaintedMap==map;
    g_lastPaintedMap=map;
    if(!g_viewportRepaint) return;
    g_viewportRepaint=false;
    if(!sameMap) return; // A newly loaded map already used the new canvas size.
    s32 centerX=(s32)gm::r32(map+0xc0)+g_previousViewportW/2;
    s32 centerY=(s32)gm::r32(map+0xc4)+g_previousViewportH/2;
    u16 speedX=gm::r16(map+0xb8),speedY=gm::r16(map+0xba);
    u8 scroll=gm::r8(map+0xbc);
    cpu.call(sym("_ZN3Map6MapSetEii"),{map,(u32)centerX,(u32)centerY});
    gm::w16(map+0xb8,speedX);gm::w16(map+0xba,speedY);gm::w8(map+0xbc,scroll);
    fprintf(stderr,"[viewport] map cache repainted\n");
}

bool Game::resizeViewport(int width, int height) {
    if(width<1 || width>1280 || height<1 || height>1280) return false;
    if(screen.w==width && screen.h==height) return true;
    // These structure fields and paint hooks are verified for the shipped ARM binary.
    if(sym("_ZN3Map5PaintEv")!=0x1b129c || gm::r32(0x1b129c)!=0xe92d4010 ||
       sym("_ZN9EventProc18Event_DialogWindowEa")!=0x16afd0) return false;
    u32 manager=cpu.call(sym("_ZN18MIDASKernelManager11getInstanceEv"),{});
    u32 oldBuffer=gm::r32(manager+0x4f0),oldPitch=gm::r32(manager+0x504);
    u32 bytesPerPixel=gm::r32(manager+0x4f4)/8;
    if(bytesPerPixel!=2 && bytesPerPixel!=4) return false;
    u32 graphic=gm::r32(sym("_lcdG"));
    u32 offscreen=graphic?gm::r32(graphic+8):0;
    if(graphic && gm::r32(graphic+4)!=manager+0x4e0) return false;
    if(offscreen && gm::r32(offscreen+0xc)/8!=bytesPerPixel) return false;
    int oldW=screen.w,oldH=screen.h;
    auto previousPixels=screen.px;
    if(!g_viewportRepaint) {g_previousViewportW=oldW;g_previousViewportH=oldH;}
    g_viewportRepaint=g_lastPaintedMap!=0;
    ++g_viewportGeneration;
    LOGI("[viewport] resize %dx%d -> %dx%d\n",oldW,oldH,width,height);
    if(offscreen && offscreen!=manager+0x4e0) {
        u32 oldData=gm::r32(offscreen+0x10);
        u32 size=(u32)width*height*bytesPerPixel;
        u32 newData=cpu.call(sym("MC_knlAlloc"),{size});
        memset(gm::ptr(newData,size),0,size);
        // Keep the framebuffer handle stable: other engine objects retain it.
        gm::w32(offscreen,width);gm::w32(offscreen+4,height);
        gm::w32(offscreen+8,width*bytesPerPixel);gm::w32(offscreen+0x10,newData);
        if(oldData && oldData!=oldBuffer) cpu.call(sym("MC_knlFree"),{oldData});
    }
    cpu.call(sym("_ZN18MIDASKernelManager14deleteTexturesEv"),{manager});
    float mag=1.0f;u32 magBits;memcpy(&magBits,&mag,4);
    cpu.callv(sym("Java_co_kr_eamobile_CletEntry_nativeInitDisplay"),
        {g_env,g_thiz,(u32)width,(u32)height,5,6,5,0,(u32)width,(u32)height,0,0,0,magBits,0,0,0,0});
    if(graphic) cpu.call(sym("_ZN7Graphic15ChangeFrameBuffEi"),{graphic,gm::r32(graphic)});
    // Retain the previous image until the engine's next timed repaint, rather
    // than showing an all-black frame when nativeLoop skips its logic tick.
    u32 newBuffer=gm::r32(manager+0x4f0),newPitch=gm::r32(manager+0x504);
    int copyW=std::min(oldW,width),copyH=std::min(oldH,height);
    int srcX=(oldW-copyW)/2,srcY=(oldH-copyH)/2;
    int dstX=(width-copyW)/2,dstY=(height-copyH)/2;
    for(int y=0;y<copyH;y++) {
        memcpy(gm::ptr(newBuffer+(dstY+y)*newPitch+dstX*bytesPerPixel,copyW*bytesPerPixel),
               gm::ptr(oldBuffer+(srcY+y)*oldPitch+srcX*bytesPerPixel,copyW*bytesPerPixel),copyW*bytesPerPixel);
        std::copy_n(previousPixels.data()+(srcY+y)*oldW+srcX,copyW,screen.px.data()+(dstY+y)*width+dstX);
    }
    if(oldBuffer && oldBuffer!=newBuffer) cpu.call(sym("MC_knlFree"),{oldBuffer});
    return screen.w==width && screen.h==height;
}

void Game::frame() {
    static auto lastTime = std::chrono::steady_clock::now();
    static double remainderMs = 0;
    auto now = std::chrono::steady_clock::now();
    double elapsedMs = std::chrono::duration<double,std::milli>(now-lastTime).count();
    lastTime=now;
    if(const char* fixed=getenv("HL5_FIXED_STEP_MS")) elapsedMs=std::clamp(atoi(fixed),1,250);
    else elapsedMs=std::min(elapsedMs,250.0);
    remainderMs+=elapsedMs;
    auto wholeMs=(u64)remainderMs;
    remainderMs-=wholeMs;
    g_simTimeMs+=wholeMs;
    cpu.call(sym("Java_co_kr_eamobile_CletEntry_nativeLoop"), {g_env, g_thiz});
}
void Game::key(int type, int k) { cpu.call(sym("Java_co_kr_eamobile_CletEntry_nativeKeyHandleEvent"), {g_env, g_thiz, (u32)type, (u32)k, 0}); }
void Game::touch(int type, int x, int y, int id) { cpu.callv(sym("Java_co_kr_eamobile_CletEntry_nativeTouchHandleEvent"), {g_env, g_thiz, (u32)type, (u32)x, (u32)y, (u32)id}); }
void Game::pause() { cpu.call(sym("Java_co_kr_eamobile_CletEntry_nativePauseApp"), {g_env, g_thiz}); }
void Game::resume() { cpu.call(sym("Java_co_kr_eamobile_CletEntry_nativeResumeApp"), {g_env, g_thiz}); }
