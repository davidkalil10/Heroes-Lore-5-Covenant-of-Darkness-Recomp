// Minimal ELF32 (ARM, shared object) loader with HLE import resolution.
#pragma once
#include "cpu.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

struct ElfImage {
    u32 base = 0;
    std::map<std::string, u32> exports;  // dynsym defined symbols -> guest addr
    std::map<std::string, u32> allsyms;  // .symtab (debug symbols) -> guest addr (if not stripped)
    std::map<u32, std::string> addr2name;
    std::vector<u32> initArray;
};

// resolve(name) must return the guest address of the HLE stub (or data) for an undefined symbol.
bool elf_load(const std::string& path, ElfImage& out, const std::function<u32(const std::string&)>& resolve);
