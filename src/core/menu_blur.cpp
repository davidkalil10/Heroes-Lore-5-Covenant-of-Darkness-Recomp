// RGB565 menu backdrop acceleration. Keep the final pixel interpreted so
// registers, flags and stack scratch match the original row exactly.
#include "cpu.h"
#include <algorithm>

bool accelerateMenuBlur(Cpu& cpu) {
    static bool oracle=false;
    static const bool disabled=getenv("HL5_NO_MENU_BLUR")!=nullptr;
    if(oracle || disabled) return false;
    const u32 sp=cpu.r[13];
    if(sp<0x1000 || u64(sp)+132>gm::MEM_SIZE) return false;
    const int width=int(cpu.r[5]),height=int(gm::r32(sp+8));
    const int diameter=int(gm::r32(sp+0x24)),y=int(gm::r32(sp+0x30));
    if(width<2 || width>4096 || height<1 || height>4096 || y<0 || y>=height ||
       diameter!=2 || gm::r32(sp+0xc)!=0 ||
       gm::r32(sp+4)!=u32(width*2) || int(gm::r32(sp+0x14))!=y-1) return false;
    static int valid=0;
    if(!valid) {
        static constexpr u32 expected[]={
    0xe92d4ff0u,
    0xe24dd084u,
    0xe28d305cu,
    0xe58d302cu,
    0xe1a04000u,
    0xe28d3044u,
    0xe59d002cu,
    0xe58d2024u,
    0xe58d303cu,
    0xebfff2a5u,
    0xe5941008u,
    0xe59d003cu,
    0xebfff2a2u,
    0xe59d002cu,
    0xebfff2c6u,
    0xe1a05000u,
    0xe59d002cu,
    0xebfff2c5u,
    0xe58d0008u,
    0xe59d003cu,
    0xebfff2c4u,
    0xe1a04000u,
    0xe59d002cu,
    0xebfff2c1u,
    0xe59d2008u,
    0xe3520000u,
    0xda000080u,
    0xe28d3074u,
    0xe58d301cu,
    0xe3a02000u,
    0xe58d2030u,
    0xe59d201cu,
    0xe1e03005u,
    0xe0803083u,
    0xe2822004u,
    0xe58d2020u,
    0xe58d3038u,
    0xe1a03085u,
    0xe58d3004u,
    0xe59d3020u,
    0xe3e02000u,
    0xe58d4034u,
    0xe2833004u,
    0xe58d2014u,
    0xe58d3028u,
    0xe3550000u,
    0xda00005eu,
    0xe59d3024u,
    0xe59d2014u,
    0xe3e0b000u,
    0xe0822003u,
    0xe58d2000u,
    0xe59d2038u,
    0xe59d3034u,
    0xe58d2010u,
    0xe3a02000u,
    0xe58d3018u,
    0xe58d200cu,
    0xe59d2014u,
    0xe59d3000u,
    0xe1520003u,
    0xe59d201cu,
    0xe3a03000u,
    0xa1a04003u,
    0xe5823000u,
    0xe59d2020u,
    0xe5823000u,
    0xe59d2028u,
    0xe5823000u,
    0xaa00002eu,
    0xe59d3024u,
    0xe59d6074u,
    0xe59de078u,
    0xe59dc07cu,
    0xe59d9010u,
    0xe59d7014u,
    0xe3a04000u,
    0xe08b0003u,
    0xe59d2008u,
    0xe1570002u,
    0xa3a03000u,
    0xb3a03001u,
    0xe3570000u,
    0xb3a03000u,
    0xe3530000u,
    0x0a000015u,
    0xe15b0000u,
    0xe1a0300bu,
    0xaa000012u,
    0xe1a02009u,
    0xe1530005u,
    0xa3a01000u,
    0xb3a01001u,
    0xe3530000u,
    0xb3a01000u,
    0xe3510000u,
    0xe2833001u,
    0x0a000006u,
    0xe1d210b0u,
    0xe2844001u,
    0xe201801fu,
    0xe201ae7eu,
    0xe08ee2aau,
    0xe08cc008u,
    0xe08665a1u,
    0xe1530000u,
    0xe2822002u,
    0xbaffffedu,
    0xe59d3000u,
    0xe59d2004u,
    0xe2877001u,
    0xe1570003u,
    0xe0899002u,
    0xbaffffdbu,
    0xe58d6074u,
    0xe58de078u,
    0xe58dc07cu,
    0xe1a01004u,
    0xe59d007cu,
    0xeb03f256u,
    0xe1a01004u,
    0xe200601fu,
    0xe59d0074u,
    0xeb03f252u,
    0xe1a01004u,
    0xe1866580u,
    0xe59d0078u,
    0xeb03f24eu,
    0xe59db00cu,
    0xe1a03280u,
    0xe2033e7eu,
    0xe28b2001u,
    0xe58d200cu,
    0xe1550002u,
    0xe59d2018u,
    0xe1863003u,
    0xe0c230b2u,
    0xe59d3010u,
    0xe58d2018u,
    0xe2833002u,
    0xe58d3010u,
    0xcaffffabu,
    0xe59d2030u,
    0xe2823001u,
    0xe58d2014u,
    0xe59d2008u,
    0xe58d3030u,
    0xe1520003u,
    0xe59d3034u,
    0xe59d2004u,
    0xe0833002u,
    0xe58d3034u,
    0xe59d3038u,
    0xe0833002u,
    0xe58d3038u,
    0xcaffff90u,
    0xe59d003cu,
    0xebfff22bu,
    0xe59d002cu,
    0xebfff229u,
    0xe28dd084u,
    0xe8bd8ff0u
        };
        bool ok=true;
        for(size_t i=0;i<sizeof(expected)/sizeof(*expected);++i)
            ok &= gm::r32(0x151980+u32(i)*4)==expected[i];
        valid=ok?1:-1;
        fprintf(stderr,"[menu-blur] opcodes validated=%d\n",ok);
    }
    if(valid<0) return false;
    const u32 window=gm::r32(sp+0x10),dest=gm::r32(sp+0x18);
    const s64 source=s64(window)-(s64(y-1)*width-1)*2;
    const u64 bytes=u64(width)*height*2;
    if(source<0x1000 || source+bytes>gm::MEM_SIZE || dest<0x1000 ||
       u64(dest)+width*2>gm::MEM_SIZE) return false;
    // Never let a malformed destination overwrite code or the active stack.
    if(dest<gm::HEAP_BASE || u64(dest)+width*2>gm::HEAP_END) return false;
    const bool verify=getenv("HL5_VERIFY_MENU_BLUR") && (y==0 || y==1 || y==height-1);
    u32 saved[16]; bool n=cpu.N,z=cpu.Z,c=cpu.C,v=cpu.V;const u64 count=cpu.icount;
    std::vector<u8> before,expectedMemory;
    u32 expectedRegs[16];bool en=false,ez=false,ec=false,ev=false;
    auto finishRow=[&] {
        oracle=true;
        unsigned steps=0;
        do {cpu.step();if(++steps>20000000) {fprintf(stderr,"[menu-blur] oracle limit\n");abort();}}
        while(cpu.r[15]!=0x151bb8);
        oracle=false;
    };
    if(verify) {
        memcpy(saved,cpu.r,sizeof(saved));before.assign(gm::mem,gm::mem+gm::MEM_SIZE);
        finishRow();
        memcpy(expectedRegs,cpu.r,sizeof(expectedRegs));en=cpu.N;ez=cpu.Z;ec=cpu.C;ev=cpu.V;
        expectedMemory.assign(gm::mem,gm::mem+gm::MEM_SIZE);
        memcpy(gm::mem,before.data(),gm::MEM_SIZE);memcpy(cpu.r,saved,sizeof(saved));
        cpu.N=n;cpu.Z=z;cpu.C=c;cpu.V=v;cpu.icount=count;
    }
    for(int x=0;x<width-1;++x) {
        unsigned red=0,green=0,blue=0,samples=0;
        for(int sy=std::max(0,y-1);sy<std::min(height,y-1+diameter);++sy) {
            for(int sx=std::max(0,x-1);sx<std::min(width,x-1+diameter);++sx) {
                const u16 pixel=gm::r16(u32(source)+(sy*width+sx)*2);
                red+=pixel>>11;green+=(pixel>>5)&63;blue+=pixel&31;++samples;
            }
        }
        // Diameter >=2 and a valid output pixel guarantee a nonempty window.
        gm::w16(dest+x*2,u16((red/samples)<<11 | (green/samples)<<5 | blue/samples));
    }
    cpu.r[11]=u32(width-2);
    gm::w32(sp+0xc,u32(width-1));
    gm::w32(sp+0x10,window+u32(width-1)*2);
    gm::w32(sp+0x18,dest+u32(width-1)*2);
    if(verify) {
        finishRow();
        if(memcmp(cpu.r,expectedRegs,sizeof(expectedRegs)) || cpu.N!=en || cpu.Z!=ez || cpu.C!=ec || cpu.V!=ev ||
           memcmp(gm::mem,expectedMemory.data(),gm::MEM_SIZE)) {
            fprintf(stderr,"[menu-blur] DIFFERENTIAL FAILURE width=%d y=%d\n",width,y);abort();
        }
        fprintf(stderr,"[menu-blur] differential PASS width=%d y=%d (registers, flags, all guest memory)\n",width,y);
        return true;
    }
    return false;
}
