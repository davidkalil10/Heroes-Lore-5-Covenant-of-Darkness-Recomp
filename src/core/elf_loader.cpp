#include "elf_loader.h"
#include <cstdio>

#pragma pack(push, 1)
struct Ehdr { u8 ident[16]; u16 type, machine; u32 version, entry, phoff, shoff, flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; };
struct Phdr { u32 type, offset, vaddr, paddr, filesz, memsz, flags, align; };
struct Shdr { u32 name, type, flags, addr, offset, size, link, info, addralign, entsize; };
struct Sym { u32 name, value, size; u8 info, other; u16 shndx; };
struct Rel { u32 offset, info; };
struct Dyn { s32 tag; u32 val; };
#pragma pack(pop)

bool elf_load(const std::string& path, ElfImage& out, const std::function<u32(const std::string&)>& resolve) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "[elf] cannot open %s\n", path.c_str()); return false; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<u8> file(sz);
    if (fread(file.data(), 1, sz, f) != (size_t)sz) { fclose(f); return false; }
    fclose(f);

    const Ehdr* eh = (const Ehdr*)file.data();
    if (memcmp(eh->ident, "\x7f""ELF", 4) != 0 || eh->machine != 40) { fprintf(stderr, "[elf] not an ARM ELF\n"); return false; }
    u32 base = gm::LOAD_BASE;
    out.base = base;

    u32 dynOff = 0, dynSz = 0;
    for (int i = 0; i < eh->phnum; i++) {
        const Phdr* ph = (const Phdr*)(file.data() + eh->phoff + i * eh->phentsize);
        if (ph->type == 1) {  // PT_LOAD
            memcpy(gm::ptr(base + ph->vaddr, ph->memsz), file.data() + ph->offset, ph->filesz);
            if (ph->memsz > ph->filesz) memset(gm::ptr(base + ph->vaddr + ph->filesz, ph->memsz - ph->filesz), 0, ph->memsz - ph->filesz);
        } else if (ph->type == 2) { dynOff = ph->vaddr; dynSz = ph->memsz; }
    }
    if (!dynOff) { fprintf(stderr, "[elf] no PT_DYNAMIC\n"); return false; }

    u32 symtab = 0, strtab = 0, rel = 0, relsz = 0, jmprel = 0, pltrelsz = 0, hash = 0, initArr = 0, initArrSz = 0;
    for (u32 o = 0; o < dynSz; o += 8) {
        Dyn d; memcpy(&d, gm::ptr(base + dynOff + o, 8), 8);
        if (d.tag == 0) break;
        switch (d.tag) {
            case 4: hash = d.val; break;
            case 5: strtab = d.val; break;
            case 6: symtab = d.val; break;
            case 17: rel = d.val; break;
            case 18: relsz = d.val; break;
            case 23: jmprel = d.val; break;
            case 2: pltrelsz = d.val; break;
            case 25: initArr = d.val; break;
            case 27: initArrSz = d.val; break;
        }
    }
    u32 nsyms = gm::r32(base + hash + 4);  // nchain
    auto symAt = [&](u32 i) { Sym s; memcpy(&s, gm::ptr(base + symtab + i * 16, 16), 16); return s; };
    auto symName = [&](u32 i) { return gm::cstr(base + strtab + symAt(i).name); };

    std::map<std::string, u32> importCache;
    auto symAddr = [&](u32 i) -> u32 {
        Sym s = symAt(i);
        if (s.shndx != 0) return base + s.value;
        std::string n = symName(i);
        auto it = importCache.find(n);
        if (it != importCache.end()) return it->second;
        u32 a = resolve(n);
        importCache[n] = a;
        return a;
    };
    for (u32 i = 1; i < nsyms; i++) {
        Sym s = symAt(i);
        if (s.shndx != 0) out.exports[symName(i)] = base + s.value;
    }

    auto applyRel = [&](u32 tbl, u32 size) {
        for (u32 o = 0; o + 8 <= size; o += 8) {
            Rel r; memcpy(&r, gm::ptr(base + tbl + o, 8), 8);
            u32 where = base + r.offset, type = r.info & 0xFF, sym = r.info >> 8;
            switch (type) {
                case 2: gm::w32(where, gm::r32(where) + symAddr(sym)); break;      // R_ARM_ABS32
                case 21: case 22: gm::w32(where, symAddr(sym)); break;             // GLOB_DAT / JUMP_SLOT
                case 23: gm::w32(where, gm::r32(where) + base); break;             // RELATIVE
                default: fprintf(stderr, "[elf] unsupported reloc type %u\n", type);
            }
        }
    };
    applyRel(rel, relsz);
    applyRel(jmprel, pltrelsz);

    for (u32 o = 0; o + 4 <= initArrSz; o += 4) out.initArray.push_back(gm::r32(base + initArr + o));

    // Optional: debug symbols (.symtab) - the HL5 binary is NOT stripped.
    const Shdr* sh = (const Shdr*)(file.data() + eh->shoff);
    for (int i = 0; i < eh->shnum; i++) {
        if (sh[i].type == 2) {  // SHT_SYMTAB
            const Shdr& str = sh[sh[i].link];
            u32 n = sh[i].size / 16;
            for (u32 k = 0; k < n; k++) {
                Sym s; memcpy(&s, file.data() + sh[i].offset + k * 16, 16);
                if ((s.info & 0xF) != 2 || s.shndx == 0) continue;  // FUNC only
                std::string nm((const char*)file.data() + str.offset + s.name);
                out.allsyms[nm] = base + s.value;
                out.addr2name[base + s.value] = nm;
            }
        }
    }
    fprintf(stderr, "[elf] loaded %s base=0x%08x dynsyms=%u exports=%zu funcs=%zu init=%zu\n", path.c_str(), base, nsyms,
            out.exports.size(), out.allsyms.size(), out.initArray.size());
    return true;
}
