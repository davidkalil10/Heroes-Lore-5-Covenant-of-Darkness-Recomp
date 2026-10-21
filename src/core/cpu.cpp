#include "cpu.h"
#include "trans.h"
#include "../native_settings.h"
extern std::string g_overrideDir;
extern void refreshViewportMap(Cpu& cpu);
extern bool handleLatinFontHook(Cpu&,u32);
extern void beginBoundedText(Cpu&);
extern void beginCoreText(Cpu&);
extern bool fitPortLabel(Cpu&,u32,int);
extern void endCoreText();
extern void endBoundedText();
extern void notifyTitlePaint();
extern void clearTitlePaint();
extern void refreshViewportDialog(Cpu& cpu);
extern bool fastVfsLookup(u32 manager,u32 name,u32 outOffset,u32& size);
// Tinted/alpha-mixed RGB565 sprite pixel used by the engine's palettized sprite blitters (0x59664 / 0x59d68).
// lutBase = guest address of the colour-adjust table, tint = (level << 5), a = {aR, aG, aB}.
static inline u16 spriteBlendPixel(u32 c, u32 dst, u32 lutBase, u32 tint, u32 aR, u32 aG, u32 aB) {
    u32 sR = c >> 11, sG = (c & 0x7e0) >> 6, sB = c & 0x1f;
    u32 dR = dst >> 11, dG = (dst & 0x7e0) >> 6, dB = dst & 0x1f;
    u32 lR = gm::r8(lutBase + 0x6d + ((tint + sR) << 5) + dR);
    u32 lG = gm::r8(lutBase + 0x6d + ((tint + sG) << 5) + dG) << 1;
    u32 lB = gm::r8(lutBase + 0x6d + ((tint + sB) << 5) + dB);
    u32 R, G, B;
    if (aR > 0x1f) { R = lR; G = lG; B = lB; }
    else {
        R = (aR * lR + (32 - aR) * dR) >> 5;
        G = (aG * lG + (64 - aG) * (dG << 1)) >> 6;
        B = (aB * lB + (32 - aB) * dB) >> 5;
    }
    return (u16)((B & 0x1f) | (R << 11) | ((G << 5) & 0x7e0));
}

bool g_prof = false;
std::vector<u32> g_profHist(0x400000);

// Fast "is this PC one of our HLE hooks" test (avoids a sparse switch on every instruction).
static std::vector<u8> g_hookMap;
static inline bool isHook(u32 pc) {
    if (g_hookMap.empty()) {
        g_hookMap.assign(0x400000, 0);
        static const u32 pcs[] = {0x1417a4, 0x11b348, 0x11feac, 0x11febc, 0x4e03c, 0x4d9f0, 0xc3bbc, 0xc42a0,
                                  0x11a1ec,0xe7d38,0x11eb3c,0x100764,0x1186e8,0xface8,0x41334,0x41470,0x42838,0x11d4b0,0x11db18,0x41a94,0x41c64,0x41510,0x40ee8,0x40f6c,0x4178c,0x4170c,0x3cf00,0x41320,0x4126c,0x41b00, 0x6afd0, 0xb129c, 0x14c7e4, 0x418dc, 0x41878, 0x41818, 0x41d78, 0x41940, 0x144db8, 0x145754, 0x59664, 0x59d68, 0x4e7e4, 0x5371c, 0x538c8};
        for (u32 p : pcs) g_hookMap[p >> 2] = 1;
    }
    u32 o = pc - 0x00100000;
    return o < 0x1000000 && g_hookMap[o >> 2];
}

u32 Cpu::addTrap(const std::string& name, TrapFn fn) {
    traps.push_back(std::move(fn));
    trapNames.push_back(name);
    return TRAP_BASE + 4 * (u32)(traps.size() - 1);
}

bool Cpu::cond(u32 c) const {
    switch (c) {
        case 0: return Z;
        case 1: return !Z;
        case 2: return C;
        case 3: return !C;
        case 4: return N;
        case 5: return !N;
        case 6: return V;
        case 7: return !V;
        case 8: return C && !Z;
        case 9: return !C || Z;
        case 10: return N == V;
        case 11: return N != V;
        case 12: return !Z && (N == V);
        case 13: return Z || (N != V);
        default: return true;
    }
}

static inline u32 ror32(u32 v, u32 n) { n &= 31; return n ? (v >> n) | (v << (32 - n)) : v; }

u32 Cpu::shiftImm(u32 v, u32 type, u32 amt, bool& c) {
    switch (type) {
        case 0:  // LSL
            if (amt == 0) return v;
            c = (v >> (32 - amt)) & 1;
            return v << amt;
        case 1:  // LSR
            if (amt == 0) { c = v >> 31; return 0; }
            c = (v >> (amt - 1)) & 1;
            return v >> amt;
        case 2:  // ASR
            if (amt == 0) { c = v >> 31; return (v >> 31) ? 0xFFFFFFFFu : 0; }
            c = (v >> (amt - 1)) & 1;
            return (u32)((s32)v >> amt);
        default:  // ROR / RRX
            if (amt == 0) { u32 res = ((u32)C << 31) | (v >> 1); c = v & 1; return res; }
            c = (v >> (amt - 1)) & 1;
            return ror32(v, amt);
    }
}

u32 Cpu::shiftReg(u32 v, u32 type, u32 amt, bool& c) {
    amt &= 0xFF;
    if (amt == 0) return v;
    switch (type) {
        case 0:
            if (amt < 32) { c = (v >> (32 - amt)) & 1; return v << amt; }
            if (amt == 32) { c = v & 1; return 0; }
            c = false; return 0;
        case 1:
            if (amt < 32) { c = (v >> (amt - 1)) & 1; return v >> amt; }
            if (amt == 32) { c = v >> 31; return 0; }
            c = false; return 0;
        case 2:
            if (amt < 32) { c = (v >> (amt - 1)) & 1; return (u32)((s32)v >> amt); }
            c = v >> 31; return (v >> 31) ? 0xFFFFFFFFu : 0;
        default:
            if ((amt & 31) == 0) { c = v >> 31; return v; }
            c = (v >> ((amt & 31) - 1)) & 1;
            return ror32(v, amt);
    }
}

u32 Cpu::addc(u32 a, u32 b, u32 cin, bool setf) {
    u64 s = (u64)a + b + cin;
    u32 res = (u32)s;
    if (setf) { C = (s >> 32) & 1; V = (((~(a ^ b)) & (a ^ res)) >> 31) & 1; }
    return res;
}

void Cpu::undef(u32 pc, u32 ins, const char* why) {
    fprintf(stderr, "[CPU] %s at pc=0x%08x ins=0x%08x (icount=%llu)\n", why, pc, ins, (unsigned long long)icount);
    for (int i = 0; i < 16; i++) fprintf(stderr, " r%d=%08x", i, r[i]);
    fprintf(stderr, "\n");
    fflush(stderr);
    abort();
}

void Cpu::step() {
    u32 pc = r[15];
    if (pc >= TRAP_BASE) {
        u32 idx = (pc - TRAP_BASE) / 4;
        if (idx >= traps.size()) undef(pc, 0, "jump into invalid trap");
        traps[idx](*this);
        u32 lr = r[14];
        if (lr & 1) undef(pc, lr, "HLE return to thumb");
        r[15] = lr;
        return;
    }
    u32 ins = gm::r32(pc);
    if (g_prof) g_profHist[(pc - 0x00100000) >> 2]++;
    if (isHook(pc)) switch (pc) {
        case 0x21a1ec:case 0x1e7d38:case 0x21eb3c:case 0x200764:case 0x2186e8:case 0x1face8:
            if(native_settings::hook(*this,pc))return;
            break;
        case 0x141334:beginCoreText(*this);break;
        case 0x141470:endCoreText();break;
        case 0x142838:case 0x21d4b0:clearTitlePaint();break;
        case 0x21db18:notifyTitlePaint();break;
        case 0x141a94:endBoundedText();break;
        case 0x141c64:case 0x141510:
        case 0x140ee8:case 0x140f6c:case 0x14178c:case 0x14170c:
        case 0x13cf00:case 0x141320:case 0x14126c:
            if(handleLatinFontHook(*this,pc))return;
            break;
        case 0x16afd0: // EventProc::Event_DialogWindow: reflow a live message after resizing.
            refreshViewportDialog(*this);
            break;
        case 0x1b129c: // Map::Paint: rebuild the resized tile cache at the next valid paint.
            refreshViewportMap(*this);
            break;
        case 0x24c7e4: {
            static bool oracle=false;
            if(oracle) break;
            u32 size;
            u32 manager=r[0],name=r[1],out=r[2];
            u32 previous=gm::r32(out);
            if(fastVfsLookup(manager,name,out,size)) {
                if(getenv("HL5_VERIFY_VFS")) {
                    u32 offset=gm::r32(out);gm::w32(out,previous);
                    oracle=true;u32 expected=call(pc,{manager,name,out});oracle=false;
                    if(size!=expected || offset!=gm::r32(out)) {fprintf(stderr,"[vfs-cache] DIFFERENTIAL FAILURE %s\n",gm::cstr(name).c_str());abort();}
                    static int verified=0;if(verified++<5)fprintf(stderr,"[vfs-cache] differential PASS %s\n",gm::cstr(name).c_str());
                }
                r[0]=size;r[15]=r[14];return;
            }
            break;
        }
        case 0x00100000 + 0x144db8: {  // engine blit(dst,src,clip): per-row loop of the 16bpp copy path, done natively
            u32 sp = r[13];
            u32 srcS = r[7], dstS = r[12];
            u32 clip = gm::r32(sp + 0x70);
            s32 w = (s32)r[11];
            if (w <= 0 || gm::r32(sp + 0x20) != 0x10 || (s32)gm::r32(clip + 0x20) <= 0xfe || gm::r32(clip + 0x2c) != 0) break;
            s32 sy = (s32)r[9], dy = (s32)gm::r32(sp + 4), dx = (s32)gm::r32(sp + 0xc), sx = (s32)gm::r32(sp + 0x68);
            s32 srcW = (s32)gm::r32(srcS), srcH = (s32)gm::r32(srcS + 4), srcPitch = (s32)gm::r32(srcS + 8);
            s32 dstW = (s32)gm::r32(dstS), dstH = (s32)gm::r32(dstS + 4), dstPitch = (s32)gm::r32(dstS + 8);
            s32 c1 = (s32)gm::r32(clip + 4), c2 = (s32)gm::r32(clip + 8), c3 = (s32)gm::r32(clip + 0xc), c4 = (s32)gm::r32(clip + 0x10);
            bool useKey = (gm::r32(sp + 0x2c) & 0xFF) != 0;
            u32 key = gm::r32(clip + 0x1c);
            u32 dstData = gm::r32(sp + 0x24), srcData = gm::r32(sp + 0x28);
            if (!(srcH > sy && dstH > dy && sy >= 0 && dy >= 0 && c2 <= dy && c4 > dy)) { /* whole row skipped */ }
            else {
                u32 srcRow = srcData + (u32)(sy * srcPitch), dstRow = dstData + (u32)(dy * dstPitch);
                for (s32 x = 0; x < w; x++) {
                    s32 px = sx + x, qx = dx + x;
                    if (srcW <= px || dstW <= qx || px < 0 || qx < 0 || c1 > qx || c3 <= qx) continue;
                    u32 v = gm::r16(srcRow + (u32)px * 2);
                    if (useKey && v == key) continue;
                    gm::w16(dstRow + (u32)qx * 2, (u16)v);
                }
            }
            icount += 14 + (u64)w * 38;
            r[4] = (u32)w; r[15] = 0x00100000 + 0x144ea4;
            return;
        }
        case 0x00100000 + 0x4e7e4: {  // solid-colour alpha overlay row on a 16bpp surface, done natively
            s32 w = (s32)r[6];
            if (w <= 0) break;
            u32 inv = r[4], addB = r[8], addG = r[10], addR = r[9], p = r[5];
            for (s32 j = 0; j < w; j++, p += 2) {
                u32 v = gm::r16(p);
                u32 g = ((v & 0x7e0) >> 3), b8 = (v << 3) & 0xff, r8 = (v & 0xf800) >> 8;
                u32 G = ((inv * g + addG) >> 5) & 0x7e0;
                u32 R = (inv * r8 + addR) & 0xf800;
                u32 B = ((inv * b8 + addB) << 16) >> 27;
                gm::w16(r[5] + (u32)j * 2, (u16)(R | G | B));
            }
            icount += (u64)w * 20 + 4;
            r[15] = 0x00100000 + 0x4e848;
            return;
        }
        case 0x00100000 + 0x5371c: {  // unTinted 8bpp palettized sprite row with colour key, native
            s32 i = (s32)r[2], cnt = (s32)r[12];
            if (i >= cnt) break;
            u32 srcP = r[1] + r[10], pal = r[7], key = r[11], d = r[0];
            for (s32 k = i; k < cnt; k++, d += 2) {
                u32 c = gm::r16(pal + (u32)gm::r8(srcP + (u32)k) * 2);
                if (c != key) gm::w16(d, (u16)c);
            }
            icount += (u64)(cnt - i) * 10;
            r[0] = d; r[2] = (u32)cnt;
            r[15] = 0x00100000 + 0x53744;
            return;
        }
        case 0x00100000 + 0x538c8: {  // unTinted 4bpp palettized sprite row with colour key, native
            s32 i = (s32)r[3], cnt = (s32)r[4];
            if (i >= cnt) break;
            u32 srcP = r[10] + r[8], pal = r[7], key = r[12], d1 = r[1], d0 = r[0];
            for (s32 k = i; k < cnt; k++, d1 += 4, d0 += 4) {
                u32 b = gm::r8(srcP + (u32)k);
                u32 c1 = gm::r16(pal + (b >> 4) * 2);
                if (c1 != key) gm::w16(d1, (u16)c1);
                u32 c2 = gm::r16(pal + (b & 0xf) * 2);
                if (c2 != key) gm::w16(d0, (u16)c2);
            }
            icount += (u64)(cnt - i) * 17;
            r[1] = d1; r[0] = d0; r[3] = (u32)cnt;
            r[15] = 0x00100000 + 0x5390c;
            return;
        }
        case 0x00100000 + 0x59664: {  // 4bpp palettized sprite row (2 px per source byte), tint LUT + alpha, done natively
            u32 sp = r[13];
            s32 i = (s32)r[2], cnt = (s32)gm::r32(sp + 0x34);
            if (i >= cnt) break;
            u32 srcP = gm::r32(sp + 0x38) + gm::r32(sp + 0x3c);
            u32 pal = r[6], key = r[8], tint = r[3], lut = r[4];
            u32 aR = gm::r8(sp + 0xac), aG = gm::r8(sp + 0xad), aB = gm::r8(sp + 0xae);
            u32 d0 = r[1];
            for (s32 k = i; k < cnt; k++, d0 += 4) {
                u32 b = gm::r8(srcP + (u32)k);
                u32 c = gm::r16(pal + (b >> 4) * 2);
                if (c != key) gm::w16(d0, spriteBlendPixel(c, gm::r16(d0), lut, tint, aR, aG, aB));
                c = gm::r16(pal + (b & 0xf) * 2);
                if (c != key) gm::w16(d0 + 2, spriteBlendPixel(c, gm::r16(d0 + 2), lut, tint, aR, aG, aB));
            }
            icount += (u64)(cnt - i) * 80;
            r[1] = d0; r[0] = d0 + 2; r[2] = (u32)cnt; r[11] = (u32)cnt;
            r[15] = 0x00100000 + 0x598a8;
            return;
        }
        case 0x00100000 + 0x59d68: {  // 8bpp palettized sprite row, tint LUT + alpha, done natively
            u32 sp = r[13];
            s32 i = (s32)r[3], cnt = (s32)r[11];
            if (i >= cnt) break;
            u32 srcP = gm::r32(sp + 0x68) + gm::r32(sp + 0x3c);
            u32 pal = gm::r32(sp + 0x34), key = gm::r32(sp + 0x38), tint = r[0], lut = r[4];
            u32 aR = gm::r8(sp + 0xac), aG = gm::r8(sp + 0xad), aB = gm::r8(sp + 0xae);
            u32 d = r[2];
            for (s32 k = i; k < cnt; k++, d += 2) {
                u32 c = gm::r16(pal + (u32)gm::r8(srcP + (u32)k) * 2);
                if (c != key) gm::w16(d, spriteBlendPixel(c, gm::r16(d), lut, tint, aR, aG, aB));
            }
            u32 n = (u32)(cnt - i);
            icount += (u64)n * 51;
            r[12] += n; r[2] = d; r[3] = (u32)cnt;
            r[1] = gm::r32(sp + 0x68);
            r[15] = 0x00100000 + 0x59e84;
            return;
        }
        case 0x00100000 + 0x145754: {  // engine fillrect(surface, clip): per-row loop natively
            u32 sp = r[13];
            u32 clip = r[6];
            u32 bpp = r[11];
            if ((bpp != 0x10 && bpp != 0x20) || (s32)gm::r32(clip + 0x20) <= 0xfe || gm::r32(clip + 0x2c) != 0) break;
            u32 surf = gm::r32(sp + 0x10), data = gm::r32(sp + 0x14);
            s32 pitch = (s32)gm::r32(surf + 8);
            s32 y = (s32)r[10], x0 = (s32)r[9], x1 = (s32)r[8];
            s32 c1 = (s32)gm::r32(clip + 4), c2 = (s32)gm::r32(clip + 8), c3 = (s32)gm::r32(clip + 0xc), c4 = (s32)gm::r32(clip + 0x10);
            u32 color = gm::r32(clip + 0x14);
            if (x0 < x1 && c2 <= y && c4 > y) {
                u32 row = data + (u32)(y * pitch);
                for (s32 x = x0; x < x1; x++) {
                    if (c1 > x || c3 <= x) continue;
                    if (bpp == 0x10) gm::w16(row + (u32)x * 2, (u16)color);
                    else gm::w32(row + (u32)x * 4, color);
                }
            }
            icount += 10 + (u64)(x1 > x0 ? x1 - x0 : 0) * 30;
            r[15] = 0x00100000 + 0x1457bc;
            return;
        }

        case 0x00100000 + 0x1417a4: { // StaticUtil::LoadRes(char* path, int* outSize)
            u32 pathPtr = r[0];
            u32 outSizePtr = r[1];
            std::string p = (pathPtr >= 0x1000 && pathPtr < gm::MEM_SIZE) ? gm::cstr(pathPtr) : "";
            std::string cleanP = (!p.empty() && p[0] == '/') ? p.substr(1) : p;
            std::string overridePath = "assets_override/" + cleanP;
            FILE* f = fopen(overridePath.c_str(), "rb");
            if (!f && !g_overrideDir.empty()) {
                std::string sep = (g_overrideDir.back() == '/') ? "" : "/";
                overridePath = g_overrideDir + sep + cleanP;
                f = fopen(overridePath.c_str(), "rb");
            }
            if (f) {
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                u32 guestBuf = gm::malloc_((u32)sz + 4);
                size_t n = fread(gm::ptr(guestBuf, (u32)sz), 1, sz, f);
                fclose(f);
                if (outSizePtr >= 0x1000 && outSizePtr < gm::MEM_SIZE) {
                    gm::w32(outSizePtr, (u32)n);
                }
                fprintf(stderr, "[OVERRIDE] Loaded '%s' from host disk (%ld bytes, guestPtr=0x%08x)\n", p.c_str(), sz, guestBuf);
                r[0] = guestBuf;
                r[15] = r[14]; // return to caller
                return;
            } else {
                fprintf(stderr, "[LoadRes] '%s' (fallback to VFS)\n", p.c_str());
            }
            break;
        }
        case 0x00100000 + 0x11b348: { // StateMainMenu::endProcess -> bypass network check
            u32 stateMainMenu = r[4];
            fprintf(stderr, "[BYPASS] Intercepted network check! Loading Title Screen directly (this=0x%08x)\n", stateMainMenu);
            // Load LogoSprite (2 = Title Screen!)
            call(0x00100000 + 0x11e368, {stateMainMenu, 2});
            // Set State 3 (DrawTitle!)
            call(0x00100000 + 0x11fbe8, {stateMainMenu, 3});

            // Pop {r4, r5, r6, r7, r8, lr} and return to caller of endProcess
            u32 sp = r[13];
            r[4] = gm::r32(sp);
            r[5] = gm::r32(sp + 4);
            r[6] = gm::r32(sp + 8);
            r[7] = gm::r32(sp + 12);
            r[8] = gm::r32(sp + 16);
            u32 lr = gm::r32(sp + 20);
            r[13] = sp + 24;
            r[15] = lr;
            return;
        }
        case 0x00100000 + 0x11feac: { // StateMainMenu::KeyTitle: cmp r1, #1
            r[1] = 1; // force substate check to pass
            break;
        }
        case 0x00100000 + 0x11febc: { // StateMainMenu::KeyTitle: carrier registered check
            r[3] = 1;
            r[15] = 0x00100000 + 0x11fef8; // skip DSP socket connect, branch to SetState(4) block
            return;
        }
        case 0x00100000 + 0x4e03c: { // DSPSock::DSP_Connect -> no-op
            r[0] = 0;
            r[15] = r[14];
            return;
        }
        case 0x00100000 + 0x4d9f0:   // DSPSock::DSP_setType
        case 0x00100000 + 0xc3bbc:   // NETWORK::CreatePatchPopup -> no-op
        case 0x00100000 + 0xc42a0: { // NETWORK::CreatePopup
            r[15] = r[14];
            return;
        }
        case 0x00100000 + 0x418dc: { // BFont::DrawText(int, int, int, int, char*, int)
            u32 strPtr = gm::r32(r[13] + 4);
            int maxLen = (r[14] == 0x00100000 + 0xe96e8) ? 13 : -1;
            u32 newPtr = Trans::translatePtr(strPtr, maxLen);
            if (newPtr != strPtr) gm::w32(r[13] + 4, newPtr);
            break;
        }
        case 0x00100000 + 0x41878:
        case 0x00100000 + 0x41818:
        case 0x00100000 + 0x41d78: {
            u32 strPtr = gm::r32(r[13] + 0);
            auto context=Locale::UiText::Full;
            switch(r[14]) {
                case 0x1fa390:case 0x21d6e4:context=Locale::UiText::Grid;break;
                case 0x1f1450:case 0x1f1a04:context=Locale::UiText::Tab;break;
                case 0x1f5864:case 0x1f5948:context=Locale::UiText::Stat;break;
                case 0x1dc818:case 0x1dcc98:case 0x1dcff4:context=Locale::UiText::Hud;break;
            }
            u32 newPtr = Trans::translatePtr(strPtr,-1,context);
            if(context==Locale::UiText::Hud && fitPortLabel(*this,newPtr,28))return;
            if (newPtr != strPtr) gm::w32(r[13] + 0, newPtr);
            break;
        }
        case 0x00100000 + 0x41b00: { // BFont::DrawTextField: translate full live dialogue before wrapping.
            u32 address=gm::r32(r[13]+8);u32 translated=Trans::translatePtr(address);
            if(translated!=address) {
                auto before=gm::cstr(address).size(),after=gm::cstr(translated).size();
                s32 visible=(s32)gm::r32(r[13]+16);
                if(visible>0 && before) gm::w32(r[13]+16,(u32)std::min<size_t>(after,(size_t(visible)*after+before-1)/before));
                gm::w32(r[13]+8,translated);
            }
            break;
        }
        case 0x00100000 + 0x41940: { // BFont::DrawTextLine
            beginBoundedText(*this);
            u32 strPtr = gm::r32(r[13] + 8);
            u32 newPtr = Trans::translatePtr(strPtr);
            if (newPtr != strPtr) gm::w32(r[13] + 8, newPtr);
            break;
        }
        default:
            break;
    }
    icount++;
    u32 cc = ins >> 28;
    u32 npc = pc + 4;
    auto rd = [&](u32 n) -> u32 { return n == 15 ? pc + 8 : r[n]; };

    if (cc == 0xF) {
        if ((ins & 0xFE000000) == 0xFA000000) {  // BLX immediate (thumb target)
            undef(pc, ins, "BLX imm to Thumb unsupported");
        }
        if ((ins & 0xFD70F000) == 0xF550F000) { r[15] = npc; return; }  // PLD
        undef(pc, ins, "unsupported unconditional instruction");
    }
    if (!cond(cc)) { r[15] = npc; return; }

    switch ((ins >> 25) & 7) {
        case 0:
        case 1: {
            bool imm = (ins >> 25) & 1;
            if (!imm && ((ins & 0x90) == 0x90 || (ins & 0x01900000) == 0x01000000)) {
                if ((ins & 0x0FFFFFF0) == 0x012FFF10) {  // BX
                    u32 t = rd(ins & 15);
                    if (t & 1) undef(pc, ins, "BX to Thumb unsupported");
                    r[15] = t; return;
                }
                if ((ins & 0x0FFFFFF0) == 0x012FFF30) {  // BLX reg
                    u32 t = rd(ins & 15);
                    if (t & 1) undef(pc, ins, "BLX to Thumb unsupported");
                    r[14] = pc + 4; r[15] = t; return;
                }
                if ((ins & 0x0FFF0FF0) == 0x016F0F10) {  // CLZ
                    u32 v = rd(ins & 15);
                    r[(ins >> 12) & 15] = v ? (u32)__builtin_clz(v) : 32;
                    r[15] = npc; return;
                }
                if ((ins & 0x0FC000F0) == 0x00000090) {  // MUL / MLA
                    u32 rdn = (ins >> 16) & 15, rn = (ins >> 12) & 15, rs = (ins >> 8) & 15, rm = ins & 15;
                    u32 res = rd(rm) * rd(rs);
                    if (ins & (1 << 21)) res += rd(rn);
                    r[rdn] = res;
                    if (ins & (1 << 20)) { N = res >> 31; Z = res == 0; }
                    r[15] = npc; return;
                }
                if ((ins & 0x0F8000F0) == 0x00800090) {  // long multiply
                    u32 hi = (ins >> 16) & 15, lo = (ins >> 12) & 15, rs = (ins >> 8) & 15, rm = ins & 15;
                    u64 res;
                    if (ins & (1 << 22)) res = (u64)((s64)(s32)rd(rm) * (s64)(s32)rd(rs));
                    else res = (u64)rd(rm) * (u64)rd(rs);
                    if (ins & (1 << 21)) res += ((u64)r[hi] << 32) | r[lo];
                    r[lo] = (u32)res; r[hi] = (u32)(res >> 32);
                    if (ins & (1 << 20)) { N = (res >> 63) & 1; Z = res == 0; }
                    r[15] = npc; return;
                }
                if ((ins & 0x0FB00FF0) == 0x01000090) {  // SWP / SWPB
                    u32 addr = rd((ins >> 16) & 15), rm = rd(ins & 15), rdn = (ins >> 12) & 15;
                    if (ins & (1 << 22)) { u32 t = gm::r8(addr); gm::w8(addr, (u8)rm); r[rdn] = t; }
                    else { u32 t = gm::r32(addr); gm::w32(addr, rm); r[rdn] = t; }
                    r[15] = npc; return;
                }
                if ((ins & 0x90) == 0x90 && (ins & 0x60) != 0) {  // halfword / signed / dword transfers
                    u32 P = (ins >> 24) & 1, U = (ins >> 23) & 1, I = (ins >> 22) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
                    u32 rn = (ins >> 16) & 15, rdn = (ins >> 12) & 15, sh = (ins >> 5) & 3;
                    u32 off = I ? (((ins >> 4) & 0xF0) | (ins & 15)) : rd(ins & 15);
                    u32 base = rd(rn);
                    u32 addr = P ? (U ? base + off : base - off) : base;
                    if (L) {
                        u32 v;
                        if (sh == 1) v = gm::r16(addr);
                        else if (sh == 2) v = (u32)(s32)(int8_t)gm::r8(addr);
                        else v = (u32)(s32)(int16_t)gm::r16(addr);
                        if (P && W) r[rn] = addr; else if (!P) r[rn] = U ? base + off : base - off;
                        if (rdn == 15) { r[15] = v & ~1u; return; }
                        r[rdn] = v;
                    } else if (sh == 1) {
                        gm::w16(addr, (u16)rd(rdn));
                        if (P && W) r[rn] = addr; else if (!P) r[rn] = U ? base + off : base - off;
                    } else if (sh == 2) {  // LDRD
                        r[rdn] = gm::r32(addr); r[rdn + 1] = gm::r32(addr + 4);
                        if (P && W) r[rn] = addr; else if (!P) r[rn] = U ? base + off : base - off;
                    } else {               // STRD
                        gm::w32(addr, rd(rdn)); gm::w32(addr + 4, rd(rdn + 1));
                        if (P && W) r[rn] = addr; else if (!P) r[rn] = U ? base + off : base - off;
                    }
                    r[15] = npc; return;
                }
                if ((ins & 0x0F900090) == 0x01000080) {  // DSP multiplies SMLAxy etc.
                    u32 op = (ins >> 21) & 3, rdn = (ins >> 16) & 15, rn = (ins >> 12) & 15, rs = (ins >> 8) & 15, rm = ins & 15;
                    bool y = (ins >> 6) & 1, x = (ins >> 5) & 1;
                    s32 a = x ? (s32)rd(rm) >> 16 : (s32)(int16_t)rd(rm);
                    s32 b = y ? (s32)rd(rs) >> 16 : (s32)(int16_t)rd(rs);
                    if (op == 0) r[rdn] = (u32)(a * b) + rd(rn);
                    else if (op == 1) {
                        s64 p = ((s64)(s32)rd(rm) * (s64)b) >> 16;
                        r[rdn] = x ? (u32)p : (u32)p + rd(rn);
                    } else if (op == 2) {
                        u64 acc = ((u64)r[rdn] << 32) | r[rn];
                        acc += (u64)(s64)((s64)a * b);
                        r[rn] = (u32)acc; r[rdn] = (u32)(acc >> 32);
                    } else r[rdn] = (u32)(a * b);
                    r[15] = npc; return;
                }
                if ((ins & 0x0F900FF0) == 0x01000050) {  // QADD/QSUB/QDADD/QDSUB
                    u32 op = (ins >> 21) & 3, rdn = (ins >> 12) & 15, rn = (ins >> 16) & 15, rm = ins & 15;
                    auto sat = [](s64 v) -> s32 { return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (s32)v; };
                    s64 a = (s32)rd(rm), b = (s32)rd(rn);
                    if (op & 2) b = sat(b * 2);
                    r[rdn] = (u32)sat((op & 1) ? a - b : a + b);
                    r[15] = npc; return;
                }
                if ((ins & 0x0FBF0FFF) == 0x010F0000) {  // MRS
                    r[(ins >> 12) & 15] = ((u32)N << 31) | ((u32)Z << 30) | ((u32)C << 29) | ((u32)V << 28) | 0x10;
                    r[15] = npc; return;
                }
                if ((ins & 0x0FB0FFF0) == 0x0120F000) {  // MSR reg
                    if ((ins >> 19) & 1) { u32 v = rd(ins & 15); N = v >> 31; Z = (v >> 30) & 1; C = (v >> 29) & 1; V = (v >> 28) & 1; }
                    r[15] = npc; return;
                }
            } else if (imm && (ins & 0x01900000) == 0x01000000) {
                if ((ins & 0x0FB0F000) == 0x0320F000) {  // MSR imm / hints (NOP)
                    if ((ins & 0x0FB00000) == 0x03200000 && ((ins >> 19) & 1) && ((ins >> 22) & 1) == 0) {
                        u32 v = ror32(ins & 0xFF, ((ins >> 8) & 15) * 2);
                        N = v >> 31; Z = (v >> 30) & 1; C = (v >> 29) & 1; V = (v >> 28) & 1;
                    }
                    r[15] = npc; return;
                }
            }
            // ---- data processing ----
            u32 opc = (ins >> 21) & 15, S = (ins >> 20) & 1;
            u32 rn = (ins >> 16) & 15, rdn = (ins >> 12) & 15;
            bool sc = C;
            u32 op2;
            if (imm) {
                u32 rot = ((ins >> 8) & 15) * 2;
                op2 = ror32(ins & 0xFF, rot);
                if (rot) sc = op2 >> 31;
            } else {
                u32 rm = ins & 15, type = (ins >> 5) & 3;
                if (ins & 0x10) {
                    u32 v = rd(rm);
                    if (rm == 15) v += 4;  // reg-shift form reads pc+12
                    u32 amt = r[(ins >> 8) & 15];
                    op2 = shiftReg(v, type, amt, sc);
                } else {
                    op2 = shiftImm(rd(rm), type, (ins >> 7) & 31, sc);
                }
            }
            u32 a = rd(rn);
            if (!imm && (ins & 0x10) && rn == 15) a += 4;
            u32 res = 0;
            bool logical = false, write = true;
            switch (opc) {
                case 0: res = a & op2; logical = true; break;
                case 1: res = a ^ op2; logical = true; break;
                case 2: res = addc(a, ~op2, 1, S); break;
                case 3: res = addc(op2, ~a, 1, S); break;
                case 4: res = addc(a, op2, 0, S); break;
                case 5: res = addc(a, op2, C, S); break;
                case 6: res = addc(a, ~op2, C, S); break;
                case 7: res = addc(op2, ~a, C, S); break;
                case 8: res = a & op2; logical = true; write = false; break;
                case 9: res = a ^ op2; logical = true; write = false; break;
                case 10: res = addc(a, ~op2, 1, true); write = false; break;
                case 11: res = addc(a, op2, 0, true); write = false; break;
                case 12: res = a | op2; logical = true; break;
                case 13: res = op2; logical = true; break;
                case 14: res = a & ~op2; logical = true; break;
                default: res = ~op2; logical = true; break;
            }
            if (S || !write) {
                if (rdn != 15 || !write) { N = res >> 31; Z = res == 0; if (logical) C = sc; }
            }
            if (write) {
                if (rdn == 15) { if (res & 1) undef(pc, ins, "data-proc branch to Thumb"); npc = res; }
                else r[rdn] = res;
            }
            r[15] = npc; return;
        }
        case 2:
        case 3: {
            if ((ins >> 25) & 1 && (ins & 0x10)) undef(pc, ins, "media/undefined instruction");
            u32 I = (ins >> 25) & 1, P = (ins >> 24) & 1, U = (ins >> 23) & 1, B = (ins >> 22) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
            u32 rn = (ins >> 16) & 15, rdn = (ins >> 12) & 15;
            u32 off;
            if (!I) off = ins & 0xFFF;
            else { bool dummy = C; off = shiftImm(rd(ins & 15), (ins >> 5) & 3, (ins >> 7) & 31, dummy); }
            u32 base = rd(rn);
            u32 eff = U ? base + off : base - off;
            u32 addr = P ? eff : base;
            if (L) {
                u32 v;
                if (B) v = gm::r8(addr);
                else { v = gm::r32(addr & ~3u); v = ror32(v, (addr & 3) * 8); }
                if (!P) r[rn] = eff; else if (W) r[rn] = eff;
                if (rdn == 15) { if (v & 1) undef(pc, ins, "LDR pc to Thumb"); r[15] = v; return; }
                r[rdn] = v;
            } else {
                u32 v = rdn == 15 ? pc + 8 : r[rdn];
                if (B) gm::w8(addr, (u8)v); else gm::w32(addr & ~3u, v);
                if (!P) r[rn] = eff; else if (W) r[rn] = eff;
            }
            r[15] = npc; return;
        }
        case 4: {  // LDM/STM
            u32 P = (ins >> 24) & 1, U = (ins >> 23) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
            u32 rn = (ins >> 16) & 15, list = ins & 0xFFFF;
            u32 n = (u32)__builtin_popcount(list);
            u32 base = r[rn];
            u32 start = U ? (P ? base + 4 : base) : (P ? base - 4 * n : base - 4 * n + 4);
            u32 wb = U ? base + 4 * n : base - 4 * n;
            u32 a = start;
            for (u32 i = 0; i < 16; i++) {
                if (!(list & (1u << i))) continue;
                if (L) {
                    u32 v = gm::r32(a);
                    if (i == 15) { if (v & 1) undef(pc, ins, "LDM pc to Thumb"); npc = v; }
                    else r[i] = v;
                } else {
                    gm::w32(a, i == 15 ? pc + 8 : r[i]);
                }
                a += 4;
            }
            if (W && !(L && (list & (1u << rn)))) r[rn] = wb;
            r[15] = npc; return;
        }
        case 5: {  // B / BL
            s32 off = (s32)(ins << 8) >> 6;
            if (ins & (1u << 24)) r[14] = pc + 4;
            r[15] = pc + 8 + off; return;
        }
        case 7:
            if (ins & (1u << 24)) {  // SWI
                // Android syscalls: unsupported ones are logged and ignored (returns 0)
                static int warned = 0;
                if (warned++ < 8) fprintf(stderr, "[CPU] swi 0x%06x r7=%u (ignored) pc=0x%08x\n", ins & 0xFFFFFF, r[7], pc);
                r[0] = 0; r[15] = npc; return;
            }
            undef(pc, ins, "coprocessor instruction unsupported");
        default:
            undef(pc, ins, "unsupported instruction class");
    }
}






#include "hot_blocks.inc"

void Cpu::run() {
    static const std::vector<u8> entryMap=[] { std::vector<u8> map(0x200000/4,0); map[(0x150208-gm::LOAD_BASE)>>2]=1; map[(0x156aa8-gm::LOAD_BASE)>>2]=1; map[(0x133f04-gm::LOAD_BASE)>>2]=1; map[(0x154dc8-gm::LOAD_BASE)>>2]=1; map[(0x13bea4-gm::LOAD_BASE)>>2]=1; map[(0x14eba0-gm::LOAD_BASE)>>2]=1; map[(0x151ae8-gm::LOAD_BASE)>>2]=1; return map; }();
    while (r[15] != RET_MAGIC) {
        u32 offset=r[15]-gm::LOAD_BASE;
        if (offset>=0x200000 || !entryMap[offset>>2] || !tryHotBlock()) step();
    }
}

u32 Cpu::call(u32 addr, std::initializer_list<u32> args) { return callv(addr, std::vector<u32>(args)); }

u32 Cpu::callv(u32 addr, const std::vector<u32>& args) {
    u32 saved[16];
    memcpy(saved, r, sizeof(r));
    bool sN = N, sZ = Z, sC = C, sV = V;
    u32 sp = r[13];
    size_t extra = args.size() > 4 ? args.size() - 4 : 0;
    sp -= (u32)(extra * 4);
    sp &= ~7u;
    for (size_t i = 0; i < extra; i++) gm::w32(sp + (u32)(4 * i), args[4 + i]);
    for (size_t i = 0; i < args.size() && i < 4; i++) r[i] = args[i];
    r[13] = sp;
    r[14] = RET_MAGIC;
    r[15] = addr;
    run();
    u32 ret = r[0];
    lastR1 = r[1];  // high word of 64-bit returns
    memcpy(r, saved, sizeof(r));
    N = sN; Z = sZ; C = sC; V = sV;
    return ret;
}
