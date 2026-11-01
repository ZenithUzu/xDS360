// SPDX-License-Identifier: GPL-3.0-or-later
// ARM->Xenon executable-memory probe. Not yet a guest CPU backend.
#include <cstdint>
#include <cstdio>
#include <xsdk/jit.h>
__asm__(".section .jit,\"awx\",@nobits\n.balign 0x10000\n.globl xds_jit_probe_pool\nxds_jit_probe_pool:\n.skip 0x10000\n.previous\n");
extern "C" uint32_t xds_jit_probe_pool[];
int main() {
    const uint32_t arms[]={0xE3A0002Au,0xE3A00063u,0xE3A00102u,0xE3A004FFu};
    const uint32_t expected[]={42,99,0x80000000u,0xFF000000u};
    FILE *report=std::fopen("game:/jit-probe.txt","w");
    bool pass=true;
    for(unsigned i=0;i<4;++i) {
        const uint32_t arm=arms[i];
        if((arm & 0xFFFFF000u)!=0xE3A00000u) {pass=false;break;}
        unsigned r=((arm>>8)&15)*2;
        uint32_t v=arm&255;
        if(r) v=(v>>r)|(v<<(32-r));
        xds_jit_probe_pool[0]=0x3C600000u|(v>>16); // lis r3,high
        xds_jit_probe_pool[1]=0x60630000u|(v&65535); // ori r3,r3,low
        xds_jit_probe_pool[2]=0x4E800020u; // blr
        xsdk_code_flush(xds_jit_probe_pool,12);
        using Fn=uint32_t(*)();
        uint32_t actual=reinterpret_cast<Fn>(xds_jit_probe_pool)();
        pass=pass && actual==expected[i];
        if(report) {std::fprintf(report,"ARM %08x -> PPC result %08x expected %08x\n",arm,actual,expected[i]);std::fflush(report);}
    }
    if(report) {std::fprintf(report,"%s\n",pass?"PASS":"FAIL");std::fclose(report);}
    return pass?0:1;
}
