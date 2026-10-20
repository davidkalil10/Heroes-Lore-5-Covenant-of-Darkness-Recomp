// ARMv5TE (ARM mode only, soft-float) interpreter used as bootstrap/oracle for the HL5 recomp.
#pragma once
#include "guest.h"
#include <functional>
#include <initializer_list>
#include <vector>

constexpr u32 TRAP_BASE = 0xF0000000;  // PC in [TRAP_BASE, ...) => HLE trap (host function)
constexpr u32 RET_MAGIC = 0xFFFF0000;  // return address that ends a host->guest call

struct Cpu;
typedef std::function<void(Cpu&)> TrapFn;

struct Cpu {
    u32 r[16] = {0};
    bool N = false, Z = false, C = false, V = false;
    u64 icount = 0;
    std::vector<TrapFn> traps;           // index = (pc-TRAP_BASE)/4
    std::vector<std::string> trapNames;

    u32 addTrap(const std::string& name, TrapFn fn);  // returns guest address of the stub
    // Calls guest function `addr` following AAPCS (args beyond 4 go on the stack). Returns r0.
    u32 call(u32 addr, std::initializer_list<u32> args);
    u32 callv(u32 addr, const std::vector<u32>& args);
    u32 lastR1 = 0;                       // r1 after the last call() (64-bit return high word)
    void run();                           // runs until pc==RET_MAGIC
    void step();
    u32 arg(int i) const { return i < 4 ? r[i] : gm::r32(r[13] + 4 * (i - 4)); }

private:
    template<u32 pc, u32 ins> void hotStep();
    bool tryHotBlock();
    bool cond(u32 c) const;
    u32 shiftImm(u32 v, u32 type, u32 amt, bool& c);
    u32 shiftReg(u32 v, u32 type, u32 amt, bool& c);
    u32 addc(u32 a, u32 b, u32 cin, bool setf);
    [[noreturn]] void undef(u32 pc, u32 ins, const char* why);
};
