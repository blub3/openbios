#!/usr/bin/env python3
"""Exercise the actual polling transfer functions with simulated EHCI DMA.
No hardware claim: MMIO, cache operations and time are replaced for the host.
Run: python3 tests/ehci-transfer-test.py
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'drivers/usbehci.c').read_text()
source = source[source.index('#define CMD'):source.index('static void start(')]
a = source.index('static void clean(')
b = source.index('/* One qTD', a)
source = source[:a] + '''static void clean(void *p, unsigned int n) { (void)p; (void)n; }
static void invalidate(void *p, unsigned int n) { (void)p; (void)n; }
''' + source[b:]
preamble = r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint32_t u32;
typedef uint8_t u8;
typedef enum { SETUP, IN, OUT } direction_t;
#define HIGH_SPEED 2
#define MIN(a,b) ((a)<(b)?(a):(b))
/* Deliberately swap even on a little-endian host to catch native accesses. */
#define __cpu_to_le32(x) __builtin_bswap32((u32)(x))
#define __le32_to_cpu(x) __builtin_bswap32((u32)(x))
#define virt_to_phys(p) ((u32)(uintptr_t)(p))
#define printk(...) ((void)0)
struct device;
typedef struct { struct device *dev; int endpoint, maxpacketsize, toggle; direction_t direction; } endpoint_t;
typedef struct { void *instance; u32 reg_base; } controller_t;
typedef struct device { controller_t *controller; endpoint_t endpoints[32]; int address, speed; } usbdev_t;
static u32 in_le32(volatile unsigned int *p);
static void out_le32(volatile unsigned int *p, u32 value);
static void mdelay(int n) { (void)n; }
'''
test = r'''
static ehci_t state;
static u32 regs[32];
static int mode, calls, pids[128], toggles[128], sizes[128], short_size;
static u32 in_le32(volatile unsigned int *p) { return regs[(uintptr_t)p / 4]; }
static void out_le32(volatile unsigned int *p, u32 value)
{
    unsigned int offset = (uintptr_t)p;
    regs[offset / 4] = value;
    if (offset != CMD) return;
    if (!(value & ASE)) {
        if (mode != 3) regs[STS/4] &= ~ASS;
        if (!(value & RUN)) regs[STS/4] |= HALTED;
        return;
    }
    regs[STS/4] |= ASS;
    u32 token = __le32_to_cpu(state.td->token);
    int length = (token >> 16) & 0x7fff;
    assert((__le32_to_cpu(state.qh->endpoint) >> 8 & 15) <= 15);
    assert((__le32_to_cpu(state.qh->endpoint) >> 12 & 3) == 2);
    assert(__le32_to_cpu(state.qh->next) == virt_to_phys(state.td));
    assert(__le32_to_cpu(state.td->buffer[0]) == virt_to_phys(state.buffer));
    for (int i=1; i<5; ++i)
        assert(__le32_to_cpu(state.td->buffer[i]) ==
               ((virt_to_phys(state.buffer) & ~4095U) + i*4096));
    pids[calls] = (token >> 8) & 3;
    toggles[calls] = token >> 31;
    sizes[calls++] = length;
    if (mode == 2) return; /* Transfer timeout. */
    if (mode == 1) { state.td->token = __cpu_to_le32(1 << 6); return; }
    int actual = short_size >= 0 ? MIN(short_size, length) : length;
    memset(state.buffer, 0xa5, actual);
    state.td->token = __cpu_to_le32((length - actual) << 16);
}
static void setup(void)
{
    memset(regs, 0, sizeof(regs)); mode=0; calls=0; short_size=-1;
    state.failed=0; regs[CMD/4]=RUN;
}
int main(void)
{
    state.qh = aligned_alloc(128, sizeof(*state.qh));
    state.td = aligned_alloc(128, sizeof(*state.td));
    state.buffer = aligned_alloc(128, BUFFER_SIZE);
    assert(sizeof(*state.qh)==128 && sizeof(*state.td)==128);
    controller_t c={ .instance=&state };
    usbdev_t d={ .controller=&c, .address=3, .speed=HIGH_SPEED };
    d.endpoints[0].maxpacketsize=64;
    endpoint_t ep={ .dev=&d, .endpoint=0x81, .maxpacketsize=512, .direction=IN };
    u8 data[20000], req[8]={0};
    setup(); memset(data,0,sizeof(data));
    assert(bulk(&ep,sizeof(data),data,0)==0);
    assert(calls==2 && sizes[0]==16384 && sizes[1]==3616);
    assert(data[0]==0xa5 && data[19999]==0xa5 && ep.toggle==0);
    setup(); short_size=512;
    assert(bulk(&ep,1024,data,0)==0 && ep.toggle==0); /* 512 + ZLP */
    setup(); short_size=13;
    assert(bulk(&ep,512,data,0)==0 && ep.toggle==1);
    setup(); ep.toggle=0; ep.direction=OUT;
    assert(bulk(&ep,512,data,1)==0 && calls==2 && sizes[1]==0 && ep.toggle==0);
    setup(); assert(control(&d,IN,8,req,130,data)==0);
    assert(calls==5 && pids[0]==2 && pids[4]==0);
    assert(toggles[0]==0 && toggles[1]==1 && toggles[2]==0 && toggles[3]==1 && toggles[4]==1);
    setup(); assert(control(&d,OUT,8,req,0,NULL)==0 && calls==2 && pids[1]==1);
    setup(); mode=1; assert(bulk(&ep,512,data,0)<0 && !(regs[STS/4]&ASS));
    setup(); mode=2; assert(bulk(&ep,512,data,0)<0 && !(regs[STS/4]&ASS));
    setup(); mode=3; assert(bulk(&ep,512,data,0)<0 && state.failed);
    assert(bulk(&ep,512,data,0)<0 && calls==1);
    free(state.qh); free(state.td); free(state.buffer);
    puts("EHCI tests passed: DMA layout, chunking, short packets, toggles, control stages, stall and timeout cleanup");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path/'test.c').write_text(preamble + source + test)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Wno-int-to-pointer-cast',
                    '-fsanitize=address,undefined', '-g', str(path/'test.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
