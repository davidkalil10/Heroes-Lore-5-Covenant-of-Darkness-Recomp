# Heroes Lore 5 - Network Verification Bypass & Offline Architecture

## 1. Context & Root Cause Analysis

When launched on Android devices in 2011, *Heroes Lore 5: Knights of the Blue* (SK Telecom release) executed an initial online handshake with EA Mobile Korea's authentication and microtransaction (MTX) server:
- **Server Address:** `210.222.18.28:11539`
- **Protocol:** Proprietary TCP packet structure with packet headers (e.g. Packet 100 for live patch verification, Packet 10 for billing status, Packet 26 for crossroad network data).

In an offline environment or native recompilation, all standard POSIX socket calls (`socket()`, `connect()`, `recv()`) return `-1` (`ENETUNREACH`).

### Assembly Trace of the Block:
1. `onStartApp` (`0x0009978c`) instantiates `StateMainMenu` with `state = 0` (`DrawCheckEvent`).
2. Inside `mainEndProcess()` (`0x00098760`), the engine calls virtual method `StateMainMenu::endProcess()` (`0x0011b198`):
   ```asm
   11b204: cmp   r0, #0            @ Check if State == 0
   11b208: bne   0x11b234
   11b20c: ldrsb r3, [r4, #16]
   11b210: cmp   r3, #0
   11b214: bne   0x11b234
   11b218: ldrb  r3, [r4, #0xf]    @ Frame counter
   11b21c: add   r3, r3, #1
   11b220: and   r3, r3, #255
   11b224: lsl   r2, r3, #24
   11b228: cmp   r2, #268435456   @ 16 ticks elapsed
   11b22c: strb  r3, [r4, #0xf]
   11b230: bgt   0x11b348
   ```
3. At `0x11b348`:
   ```asm
   11b348: mov   r3, #1
   11b34c: strb  r3, [r4, #0x10]
   11b350: ldr   r3, [pc, #0x24]
   11b354: mov   r1, #9           @ Connection mode 9
   11b358: ldr   r3, [r6, r3]
   11b35c: ldr   r0, [r3]         @ NETWORK singleton
   11b360: pop   {r4, r5, r6, r7, r8, lr}
   11b364: b     0xc6428 <NETWORK::netConnect(signed char)>
   ```
4. In `NETWORK::netConnect(signed char)` (`0x000c6428`):
   - Sets network state to 9 via `SetNetConnectState(9)`.
   - Calls `InitNetwork()` (`0xc3fac`) and `netOpen()` (`0xc63c4`).
   - Creates Popup 339 via `NETWORK::CreatePopup(339)`.
5. Simultaneously, `NETWORK::WriteData(int, int, unsigned int)` (`0x000c6604`) attempts to write packet 100:
   ```asm
   c660c: ldrb  r0, [r0, #0x358]  @ isNetConnected flag
   c6614: cmp   r0, #0
   c6618: beq   0xc67ec
   ...
   c67f4: cmp   r5, #100          @ Packet 100
   c67fc: beq   0xc6814
   ...
   c6824: b     0xc3bbc <NETWORK::CreatePatchPopup(char*)>
   ```
6. In `NETWORK::CreatePatchPopup(char*)` (`0x000c3bbc`):
   - Calls `StateMainMenu::SetPopupState(11, ...)` (`0x117d78`), which creates `StatePopup` with message `"Connecting"`.
   - The game loop enters `StatePopup::Process()`, where it remains permanently awaiting server response packets that will never arrive.

---

## 2. Technical Bypass Implementation

Rather than emulating a mock server with fake TCP packets, the recompilation decouples the engine logic directly at the assembly level in `src/core/cpu.cpp`:

```cpp
// 1. Intercept the network trigger inside StateMainMenu::endProcess()
if (pc == 0x00100000 + 0x11b348) {
    u32 stateMainMenu = r[4]; // 'this' pointer preserved in r4
    fprintf(stderr, "[BYPASS] Intercepted network check! Loading Title Screen directly (this=0x%08x)\n", stateMainMenu);

    // Call StateMainMenu::LoadLogoSprite(2) -> loads Title sprites (/c/sp/imgcom/title.mgr, /c/sp/cif/title.cif)
    call(0x00100000 + 0x11e368, {stateMainMenu, 2});

    // Call StateMainMenu::SetState(3) -> transitions directly to State 3 (Title Screen!)
    call(0x00100000 + 0x11fbe8, {stateMainMenu, 3});

    // Cleanly unwind stack frame {r4, r5, r6, r7, r8, lr}
    u32 sp = r[13];
    r[4] = gm::r32(sp);
    r[5] = gm::r32(sp + 4);
    r[6] = gm::r32(sp + 8);
    r[7] = gm::r32(sp + 12);
    r[8] = gm::r32(sp + 16);
    u32 lr = gm::r32(sp + 20);
    r[13] = sp + 24;
    r[15] = lr; // Return cleanly to caller (mainEndProcess)
    return;
}

// 2. Prevent any secondary network popup from ever opening
if (pc == 0x00100000 + 0xc3bbc) { // NETWORK::CreatePatchPopup
    fprintf(stderr, "[NETWORK] CreatePatchPopup blocked (lr=0x%08x)\n", r[14] - 0x00100000);
    r[15] = r[14]; // bx lr
    return;
}

if (pc == 0x00100000 + 0xc42a0) { // NETWORK::CreatePopup
    fprintf(stderr, "[NETWORK] CreatePopup id=%d blocked (lr=0x%08x)\n", r[1], r[14] - 0x00100000);
    r[15] = r[14]; // bx lr
    return;
}
```

---

## 3. Verification & Results

- **Before Bypass:** Frame 1600+ displayed `dumps/frame_1600.png` showing a single dialog box with text `"Connecting"`, hanging indefinitely.
- **After Bypass:**
  - `[BYPASS]` logs cleanly.
  - The game transitions straight to the Title Screen.
  - Output dumps (`dumps_title_final/final.png`) confirm full 1129-color rendering with background art, title logo, and ready state (`substate = 1`).
  - Total time from binary launch to title screen: **1.8 seconds (120 frames)**.
