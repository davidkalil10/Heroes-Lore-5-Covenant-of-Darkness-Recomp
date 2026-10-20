#include "guest.h"
#include <map>

namespace gm {
u8* mem = nullptr;

static std::map<u32, u32> g_free;   // addr -> size
static std::map<u32, u32> g_alloc;  // addr -> size
static u32 g_top = HEAP_BASE;

void init() {
    mem = (u8*)calloc(MEM_SIZE, 1);
    if (!mem) { fprintf(stderr, "cannot allocate guest memory\n"); exit(1); }
}

static void (*g_dumper)() = nullptr;

void registerCpuDumper(void (*dumper)()) {
    g_dumper = dumper;
}

void fault(const char* what, u32 addr) {
    fprintf(stderr, "[GUEST FAULT] %s at 0x%08x\n", what, addr);
    if (g_dumper) g_dumper();
    fflush(stderr);
    abort();
}

std::string cstr(u32 a) {
    std::string s;
    while (true) { char c = (char)r8(a++); if (!c) break; s.push_back(c); }
    return s;
}

u32 malloc_(u32 n) {
    u32 sz = (n + 15u) & ~15u;
    if (sz < 16) sz = 16;
    for (auto it = g_free.begin(); it != g_free.end(); ++it) {
        if (it->second >= sz) {
            u32 a = it->first, rest = it->second - sz;
            g_free.erase(it);
            if (rest) g_free[a + sz] = rest;
            g_alloc[a] = sz;
            return a;
        }
    }
    if ((u64)g_top + sz > HEAP_END) fault("guest heap exhausted", n);
    u32 a = g_top;
    g_top += sz;
    g_alloc[a] = sz;
    return a;
}

void free_(u32 p) {
    if (!p) return;
    auto it = g_alloc.find(p);
    if (it == g_alloc.end()) { fprintf(stderr, "[heap] bad free 0x%08x\n", p); return; }
    u32 sz = it->second;
    g_alloc.erase(it);
    auto nx = g_free.lower_bound(p);
    if (nx != g_free.end() && p + sz == nx->first) { sz += nx->second; nx = g_free.erase(nx); }
    if (nx != g_free.begin()) {
        auto pv = std::prev(nx);
        if (pv->first + pv->second == p) { pv->second += sz; return; }
    }
    g_free[p] = sz;
}

u32 put_cstr(const std::string& s) {
    u32 a = malloc_((u32)s.size() + 1);
    memcpy(ptr(a, (u32)s.size() + 1), s.c_str(), s.size() + 1);
    return a;
}
}  // namespace gm
