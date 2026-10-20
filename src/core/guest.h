// Guest (ARM32) flat memory model for the HL5 recomp bootstrap emulator.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int64_t s64;

namespace gm {
constexpr u32 MEM_SIZE = 0x08000000;     // 128 MiB guest address space
constexpr u32 LOAD_BASE = 0x00100000;    // .so load base
constexpr u32 STACK_TOP = 0x07F00000;    // stack grows down (1 MiB)
constexpr u32 HEAP_BASE = 0x01000000;    // malloc arena [HEAP_BASE, HEAP_END)
constexpr u32 HEAP_END = 0x07000000;

extern u8* mem;
void init();
void registerCpuDumper(void (*dumper)());
[[noreturn]] void fault(const char* what, u32 addr);

inline u8* ptr(u32 a, u32 n = 1) {
    if (a < 0x1000 || (u64)a + n > MEM_SIZE) fault("bad access", a);
    return mem + a;
}
inline u32 r32(u32 a) { u32 v; memcpy(&v, ptr(a, 4), 4); return v; }
inline u16 r16(u32 a) { u16 v; memcpy(&v, ptr(a, 2), 2); return v; }
inline u8 r8(u32 a) { return *ptr(a, 1); }
inline void w32(u32 a, u32 v) { memcpy(ptr(a, 4), &v, 4); }
inline void w16(u32 a, u16 v) { memcpy(ptr(a, 2), &v, 2); }
inline void w8(u32 a, u8 v) { *ptr(a, 1) = v; }
std::string cstr(u32 a);
u32 put_cstr(const std::string& s);  // malloc + copy (guest heap)

// guest heap
u32 malloc_(u32 n);
void free_(u32 p);
}  // namespace gm
