/*
 *   Creation Date: <2004/08/28 18:38:22 greg>
 *   Time-stamp: <2004/08/28 18:38:22 greg>
 *
 *	<init.c>
 *
 *	Initialization for Xbox 360
 *
 *   Copyright (C) 2004 Greg Watson
 *   Copyright (C) 2005 Stefan Reinauer
 *   Copyright (C) 2026 John Davis
 *
 *   based on mol/init.c:
 *
 *   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004 Samuel & David Rydh
 *      (samuel@ibrium.se, dary@lindesign.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include "config.h"
#include "libopenbios/openbios.h"
#include "libopenbios/bindings.h"
#include "libopenbios/console.h"
#include "drivers/pci.h"
#include "drivers/usb.h"
#include "arch/common/nvram.h"
#include "drivers/drivers.h"
#include "xbox360/xbox360.h"
#include "libopenbios/ofmem.h"
#include "openbios-version.h"
#include "libc/byteorder.h"
#include "libc/vsprintf.h"
#define NO_QEMU_PROTOS
#include "arch/ppc/processor.h"

#include "../../drivers/timer.h" // TODO
#include "NativeHDMI.h"


extern void unexpected_excep(int vector, unsigned long nip, unsigned long status);

static inline uint64_t
in_be64(volatile uint64_t *addr)
{
	uint32_t hi, lo;

	__asm__ __volatile__("ld %1,0(%2); eieio; srdi %0,%1,32":"=r"(hi),"=r"(lo):"b"(addr));
	return ((uint64_t)hi << 32) | lo;
}

static inline void
out_be64(volatile uint64_t *addr, uint64_t val)
{
    uint32_t hi = (uint32_t)(val >> 32);
    uint32_t lo = (uint32_t)(val);
    uint32_t tmp;

    __asm__ __volatile__(
        "rldicl %0, %2, 0, 32\n\t"   // Clear any garbage in the upper 32 bits of 'lo', store in 'tmp'
        "rldimi %0, %1, 32, 0\n\t"   // Shift 'hi' left 32 bits and insert it into the upper half of 'tmp'
        "std %0, 0(%3)\n\t"          // Store the combined 64-bit register to memory
        "eieio\n\t"
        : "=&r"(tmp)                 // Early-clobber output for our combined 64-bit value
        : "r"(hi), "r"(lo), "b"(addr) // 'b' constraint ensures a valid base register for the offset
        : "memory"
    );
}

void
unexpected_excep(int vector, unsigned long nip, unsigned long status)
{
    printk("openbios panic: Unexpected exception %x at %lx SRR1=%lx\n",
           vector, nip, status);
    for (;;) {
    }
}

extern void __divide_error(void);

void
__divide_error(void)
{
    return;
}

unsigned long isa_io_base;

extern struct _console_ops xbox360_console_ops;

int is_apple(void)
{
    return 1;
}

int is_oldworld(void)
{
    return 0;
}

int is_newworld(void)
{
    return 1;
}

static const pci_arch_t xbox360_arch = {
    .name = "XENON",
    .vendor_id = PCI_VENDOR_ID_MICROSOFT,
    .device_id = PCI_DEVICE_ID_MICROSOFT_XENON_HOST_XENON,
    .cfg_addr = 0xD0000000,
    .cfg_data = 0xD0000000,
    .cfg_base = 0xD0000000,
    .cfg_len = 0x1000000,
    .host_pci_base = 0x0,
    .pci_mem_base = 0xEA000000,
    .mem_len = 0x10000000,
    .io_base = 0,
    .io_len = 0,
    .host_ranges = {
        { .type = MEMORY_SPACE_32, .parentaddr = 0, .childaddr = 0x80000000, .len = 0x80000000 },
        { .type = 0, .parentaddr = 0, .childaddr = 0, .len = 0 }
    },
    .irqs = { 15, 15, 15, 15 }
};

typedef union {
    unsigned char   data[16];
    unsigned long   data32[4];
} xbox360_smc_msg;

static void
xbox360_smc_write_ready(xbox360_smc_msg *msg)
{
    out_le32((volatile unsigned int*)(XBOX360_SMC_BASE + 0x84), 0x4);
    for (int i = 0; i < 4; i++) {
        out_be32((volatile unsigned int*)(XBOX360_SMC_BASE + 0x80), msg->data32[i]);
    }
    out_le32((volatile unsigned int*)(XBOX360_SMC_BASE + 0x84), 0x0);
}

static void
xbox360_smc_write(xbox360_smc_msg *msg)
{
    while ((in_le32((volatile unsigned int*)(XBOX360_SMC_BASE + 0x84)) & 0x4) == 0);
    xbox360_smc_write_ready(msg);
}

/* SMC I2C replies are synchronous here, before the OS owns the mailbox. */
static int
xbox360_smc_i2c(xbox360_smc_msg *msg)
{
    volatile unsigned int *send_status = (volatile unsigned int *)(XBOX360_SMC_BASE + 0x84);
    volatile unsigned int *recv_status = (volatile unsigned int *)(XBOX360_SMC_BASE + 0x94);
    volatile unsigned int *recv_data = (volatile unsigned int *)(XBOX360_SMC_BASE + 0x90);
    unsigned int ms;

    for (ms = 0; ms < 100 && !(in_le32(send_status) & 4); ms++)
        mdelay(1);
    if (ms == 100)
        return 0;
    xbox360_smc_write_ready(msg);

    for (ms = 0; ms < 100; ms++) {
        if (in_le32(recv_status) & 4) {
            out_le32(recv_status, 4);
            for (unsigned int word = 0; word < 4; word++)
                msg->data32[word] = in_be32(recv_data);
            out_le32(recv_status, 0);
            if (msg->data[0] == 0x11)
                return msg->data[1] == 0;
        }
        mdelay(1);
    }
    return 0;
}

static int
xbox360_hdmi_write(uint8_t reg, uint8_t value)
{
    xbox360_smc_msg msg = { .data = { 0x11, 0x20, 0, 0xbd, 0, 0, reg, value } };
    return xbox360_smc_i2c(&msg);
}

static int
xbox360_hdmi_read(uint8_t reg, uint8_t *value)
{
    xbox360_smc_msg msg = { .data = { 0x11, 0x10, 1, 0xbd, 0, 0xbd, reg } };
    if (!xbox360_smc_i2c(&msg))
        return 0;
    *value = msg.data[3];
    return 1;
}

static int
xbox360_ana_read(uint8_t reg, uint32_t *value)
{
    xbox360_smc_msg msg = { .data = { 0x11, 0x10, 5, 0xf0, 0, 0xf0, reg } };
    if (!xbox360_smc_i2c(&msg))
        return 0;
    *value = (uint32_t)msg.data[4] | ((uint32_t)msg.data[5] << 8) |
        ((uint32_t)msg.data[6] << 16) | ((uint32_t)msg.data[7] << 24);
    return 1;
}

static int
xbox360_ana_write(uint8_t reg, uint32_t value)
{
    xbox360_smc_msg msg = { .data = { 0x11, 0x60, 0, 0xf0, 0, 0, reg, 0,
                                     value & 0xff, (value >> 8) & 0xff,
                                     (value >> 16) & 0xff, value >> 24 } };
    return xbox360_smc_i2c(&msg);
}

/*
 * Configure HDMI stereo PCM through the same SMC I2C interface used by
 * Free60 libxenon (libxenon/drivers/xenon_sound/sound.c). Run only on
 * request, with an HDMI display attached, before starting Mac OS X.
 */
static void
xbox360_hdmi_audio(void)
{
    static const struct {
        uint8_t reg;
        uint8_t value;
    } setup[] = {
        { 0x14, 0x11 }, { 0x02, 0x03 }, { 0x03, 0x00 },
        { 0x04, 0x18 }, { 0x05, 0x00 }, { 0x1d, 0x40 },
        { 0x21, 0x02 }, { 0x22, 0x2b }, { 0x2f, 0x01 },
        { 0x3e, 0x3f }, { 0xdf, 0x10 }
    };
    char result[80];
    uint8_t readback;
    unsigned int i;

    /*
     * Use the known working audio enable command after mode setting.
     * The value observed in a running transmitter is a readback, and
     * may not trigger the same operation when written.
     */
    for (i = 0; i < sizeof(setup) / sizeof(setup[0]); i++)
        if (!xbox360_hdmi_write(setup[i].reg, setup[i].value))
            break;

    if (i < sizeof(setup) / sizeof(setup[0])) {
        snprintf(result, sizeof(result), "SMC write failed at register 02%02x", setup[i].reg);
    } else if (xbox360_hdmi_read(0x14, &readback)) {
        snprintf(result, sizeof(result), "transmitter setup complete; HDMI 0214 wrote 11, read %02x", readback);
    } else {
        snprintf(result, sizeof(result), "transmitter setup complete; HDMI 0214 read failed");
    }
    phandle_t chosen = find_dev("/chosen");
    if (chosen)
        set_property(chosen, "xbox360-hdmi-audio-test", result, strlen(result) + 1);
    printk("HDMI audio: %s\n", result);
}

static void
xbox360_reset_all(void)
{
    xbox360_smc_msg msg;
    bzero (&msg, sizeof (msg));

    msg.data[0] = 0x82;
    msg.data[1] = 0x04;
    msg.data[2] = 0x31;

    xbox360_smc_write(&msg);
}

static void
xbox360_poweroff(void)
{
    xbox360_smc_msg msg;
    bzero (&msg, sizeof (msg));

    msg.data[0] = 0x82;
    msg.data[1] = 0x01;

    xbox360_smc_write(&msg);
}

unsigned int xbox360_video_width = 1280;
unsigned int xbox360_video_height = 720;
static char xbox360_scanout_info[120];
static XenonHDMIProfiles native_hdmi;

static uint32_t
xbox360_gpu_read(unsigned int reg)
{
    return in_be32((volatile uint32_t *)(XBOX360_GPU_BASE + reg));
}

static void
xbox360_read_video_geometry(void)
{
    unsigned int width = xbox360_gpu_read(0x6134);  /* D1GRPH_X_END */
    unsigned int height = xbox360_gpu_read(0x6138); /* D1GRPH_Y_END */
    unsigned int pitch = xbox360_gpu_read(0x6120);  /* D1GRPH_PITCH, in pixels */
    unsigned int htotal = xbox360_gpu_read(0x6000) & 0x1fff;
    unsigned int vtotal = xbox360_gpu_read(0x6020) & 0x1fff;
    unsigned int sync_b = xbox360_gpu_read(0x6010) & 0x1fff;
    unsigned int format = xbox360_gpu_read(0x6528); /* D1MODE_DATA_FORMAT */
    unsigned int control = xbox360_gpu_read(0x6104); /* D1GRPH_CONTROL */

    /*
     * Inherit a matching 1080p50/60 scanout. A scaled 720p surface does not
     * qualify, even when the sink is receiving a 1080p signal.
     */
    if (width == 1920 && height == 1080 && pitch == width &&
        (htotal == 2199 || htotal == 2639) &&
        (vtotal == 1124 || sync_b == 1124) && !(format & 1) &&
        control == 2 && xbox360_gpu_read(0x6124) == 0 &&
        xbox360_gpu_read(0x6128) == 0 && xbox360_gpu_read(0x612c) == 0 &&
        xbox360_gpu_read(0x6130) == 0) {
        xbox360_video_width = width;
        xbox360_video_height = height;
    }

    snprintf(xbox360_scanout_info, sizeof(xbox360_scanout_info),
             "%ux%u pitch=%u htotal=%u vtotal=%u sync-b=%u format=%x control=%x",
             width, height, pitch, htotal, vtotal, sync_b, format, control);
}

static void
xbox360_gpu_write(unsigned int reg, uint32_t value)
{
    out_be32((volatile uint32_t *)(XBOX360_GPU_BASE + reg), value);
}

/*
 * Experimental 1080p60 transition from the Slim's known 720p boot mode.
 * The clock/timing values came from paired retail v3 captures. Keep the
 * remaining transmitter state and use a native surface without scaling.
 */
static const char *xbox360_try_1080p(void)
{
    static const struct { uint8_t reg; uint32_t value; } ana[] = {
        { 0xd0, 0x000559d5 }, { 0xd1, 0x000259d5 },
        { 0x12, 0xf852d750 }, { 0x13, 0x60780898 },
        { 0x14, 0x0006d02d }, { 0x15, 0x52020465 }
    };
    static const struct { uint8_t reg; uint8_t value; } hdmi[] = {
        { 0x03, 0x00 }, { 0x04, 0x18 }, { 0x14, 0x03 }
    };
    static const struct { uint16_t reg; uint32_t value; } timing[] = {
        { 0x6000, 0x897 }, { 0x6004, 0x00bf083f },
        { 0x6010, 0x464 }, { 0x6014, 0x00290461 }
    };
    uint32_t old_ana[sizeof(ana) / sizeof(ana[0])], control, readback;
    uint8_t old_hdmi[sizeof(hdmi) / sizeof(hdmi[0])], hdmi_value;
    unsigned int a = 0, h = 0;
    int restored = 1;

    if (xbox360_video_width == 1920)
        return "inherited 1080p";
    if (xbox360_gpu_read(0x6134) != 1280 || xbox360_gpu_read(0x6138) != 720 ||
        xbox360_gpu_read(0x6120) != 1280 || xbox360_gpu_read(0x6104) != 2 ||
        (xbox360_gpu_read(0x6000) & 0x1fff) != 1649 ||
        (xbox360_gpu_read(0x6010) & 0x1fff) != 749 ||
        (xbox360_gpu_read(0x6528) & 1))
        return "skipped: inherited scanout is not the tested 720p mode";

    if (!xbox360_ana_read(0, &control) || control != 0x2c1)
        return "skipped: HDMI transmitter not in the expected active state";
    for (unsigned int i = 0; i < sizeof(ana) / sizeof(ana[0]); i++)
        if (!xbox360_ana_read(ana[i].reg, &old_ana[i]))
            return "skipped: could not save ANA settings";
    for (unsigned int i = 0; i < sizeof(hdmi) / sizeof(hdmi[0]); i++)
        if (!xbox360_hdmi_read(hdmi[i].reg, &old_hdmi[i]))
            return "skipped: could not save HDMI settings";

    /* Save the original timing while XeLL's 720p HDMI output is active. */
    for (unsigned int i = 0; i < sizeof(ana) / sizeof(ana[0]); i++) {
        native_hdmi.mode720.ana[i] = old_ana[i];
        native_hdmi.mode1080.ana[i] = ana[i].value;
    }
    for (unsigned int i = 0; i < sizeof(timing) / sizeof(timing[0]); i++) {
        native_hdmi.mode720.timing[i] = xbox360_gpu_read(timing[i].reg);
        native_hdmi.mode1080.timing[i] = timing[i].value;
    }

    /* Disable the transmitter before changing its PLL and digital timing. */
    if (!xbox360_ana_write(0, 0))
        goto restore;
    while (a < sizeof(ana) / sizeof(ana[0])) {
        unsigned int i = a++;
        if (!xbox360_ana_write(ana[i].reg, ana[i].value) ||
            !xbox360_ana_read(ana[i].reg, &readback) || readback != ana[i].value)
            goto restore;
    }
    mdelay(5);
    while (h < sizeof(hdmi) / sizeof(hdmi[0])) {
        unsigned int i = h++;
        if (!xbox360_hdmi_write(hdmi[i].reg, hdmi[i].value) ||
            !xbox360_hdmi_read(hdmi[i].reg, &hdmi_value) || hdmi_value != hdmi[i].value)
            goto restore;
    }
    if (!xbox360_ana_write(0, control))
        goto restore;

    /* Preserve the inherited pixel format and framebuffer addresses. */
    xbox360_gpu_write(0x60e8, 1);  /* CRTC update lock */
    for (unsigned int i = 0; i < sizeof(timing) / sizeof(timing[0]); i++)
        xbox360_gpu_write(timing[i].reg, timing[i].value);
    xbox360_gpu_write(0x60e8, 0);

    xbox360_gpu_write(0x6144, 1);  /* Graphics surface update lock */
    xbox360_gpu_write(0x6120, 1920);
    xbox360_gpu_write(0x6134, 1920);
    xbox360_gpu_write(0x6138, 1080);
    xbox360_gpu_write(0x6144, 0);

    xbox360_gpu_write(0x65cc, 1);  /* Scaler update lock */
    xbox360_gpu_write(0x2844, 1920);
    xbox360_gpu_write(0x652c, 1080);
    xbox360_gpu_write(0x6580, 0);
    xbox360_gpu_write(0x6584, (1920u << 16) | 1080u);
    xbox360_gpu_write(0x6590, 0);  /* Bypass scaling at native resolution */
    xbox360_gpu_write(0x65e8, 1920 / 4 - 1);
    xbox360_gpu_write(0x65cc, 0);

    xbox360_video_width = 1920;
    xbox360_video_height = 1080;
    native_hdmi.control = control;
    native_hdmi.version = XENON_NATIVE_HDMI_VERSION;
    return "programmed experimental 1080p60";

restore:
    while (h) {
        --h;
        restored &= xbox360_hdmi_write(hdmi[h].reg, old_hdmi[h]);
    }
    while (a) {
        --a;
        restored &= xbox360_ana_write(ana[a].reg, old_ana[a]);
    }
    restored &= xbox360_ana_write(0, control);
    return restored ? "720p retained after SMC failure" :
        "SMC failure and incomplete transmitter rollback";
}

static void
xbox360_boot_is_1080p(void)
{
    PUSH(xbox360_video_width == 1920 ? -1 : 0);
}

void
entry(void)
{
    isa_io_base = 0x80000000;

    arch = &xbox360_arch;

    xbox360_read_video_geometry();

    // Switch framebuffer to new location
    out_be32((unsigned int*)(XBOX360_GPU_BASE + 0x6144), 1);
    out_be32((unsigned int*)(XBOX360_GPU_BASE + 0x6110), XBOX360_FB_BASE);
    out_be32((unsigned int*)(XBOX360_GPU_BASE + 0x6144), 0);

    out_be32((unsigned int*)(XBOX360_GPU_BASE + 0x65cc), 1);
    out_be32((unsigned int*)(XBOX360_GPU_BASE + 0x2840), XBOX360_FB_BASE);
    out_be32((unsigned int*)(XBOX360_GPU_BASE + 0x65cc), 0);

#ifdef CONFIG_DEBUG_CONSOLE
    init_console(xbox360_console_ops);
#endif

    ofmem_init();
    initialize_forth();
    /* won't return */

    printk("of_startup returned!\n");
    for (;;) {
    }
}

/* -- phys.lo ... phys.hi */
static void
push_physaddr(phys_addr_t value)
{
    PUSH(value);
}

/* From drivers/timer.c */
extern unsigned long timer_freq;

static void
memory_xenon_init(void)
{
    push_str("/memory");
    fword("find-device");

    push_physaddr(0);
    fword("encode-phys");

    PUSH(XBOX360_RAM_SIZE);
    fword("encode-int");
    fword("encode+");
    push_str("reg");
    fword("property");

    push_str(XBOX360_RAM_TYPE);
    fword("encode-string");
    push_str("dimm-types");
    fword("property");
}

static void
cpu_xenon_init(void)
{
    push_str("/cpus");
    fword("find-device");

    fword("new-device");

    push_str(XBOX360_CPU_NAME);
    fword("device-name");

    push_str("cpu");
    fword("device-type");

    PUSH(mfpvr());
    fword("encode-int");
    push_str("cpu-version");
    fword("property");

    PUSH(0x8000);
    fword("encode-int");
    push_str("d-cache-size");
    fword("property");

    PUSH(0x8000);
    fword("encode-int");
    push_str("i-cache-size");
    fword("property");

    PUSH(0x80);
    fword("encode-int");
    push_str("d-cache-sets");
    fword("property");

    PUSH(0x80);
    fword("encode-int");
    push_str("i-cache-sets");
    fword("property");

    PUSH(0x20);
    fword("encode-int");
    push_str("d-cache-block-size");
    fword("property");

    PUSH(0x20);
    fword("encode-int");
    push_str("i-cache-block-size");
    fword("property");

    PUSH(0x40);
    fword("encode-int");
    push_str("tlb-sets");
    fword("property");

    PUSH(0x80);
    fword("encode-int");
    push_str("tlb-size");
    fword("property");

    timer_freq = XBOX360_TIMEBASE_FREQ;
    PUSH(timer_freq);
    fword("encode-int");
    push_str("timebase-frequency");
    fword("property");

    PUSH(XBOX360_CPU_FREQ);
    fword("encode-int");
    push_str("clock-frequency");
    fword("property");

    PUSH(XBOX360_TIMEBASE_FREQ * 4);
    fword("encode-int");
    push_str("bus-frequency");
    fword("property");

    push_str("running");
    fword("encode-string");
    push_str("state");
    fword("property");

    PUSH(0x20);
    fword("encode-int");
    push_str("reservation-granule-size");
    fword("property");

    PUSH(0);
    fword("encode-int");
    push_str("reg");
    fword("property");

    fword("finish-device");
}


static void arch_go(void);

static void
arch_go(void)
{
    /* Insert copyright property for MacOS 9 and below */
    if (find_dev("/rom/macos")) {
        fword("insert-copyright-property");
    }
}

/*
 *  filll        ( addr bytes quad -- )
 */

static void ffilll(void)
{
    const u32 longval = POP();
    u32 bytes = POP();
    u32 *laddr = (u32 *)cell2pointer(POP());
    u32 len;

    for (len = 0; len < bytes / sizeof(u32); len++) {
        *laddr++ = longval;
    }
}

/*
 * adler32        ( adler buf len -- checksum )
 *
 * Adapted from Mark Adler's original implementation (zlib license)
 *
 * Both OS 9 and BootX require this word for payload validation.
 */

#define DO1(buf,i)  {s1 += buf[i]; s2 += s1;}
#define DO2(buf,i)  DO1(buf,i); DO1(buf,i+1);
#define DO4(buf,i)  DO2(buf,i); DO2(buf,i+2);
#define DO8(buf,i)  DO4(buf,i); DO4(buf,i+4);
#define DO16(buf)   DO8(buf,0); DO8(buf,8);

static void adler32(void)
{
    uint32_t len = (uint32_t)POP();
    char *buf = (char *)POP();
    uint32_t adler = (uint32_t)POP();

    if (buf == NULL) {
        RET(-1);
    }

    uint32_t base = 65521;
    uint32_t nmax = 5552;

    uint32_t s1 = adler & 0xffff;
    uint32_t s2 = (adler >> 16) & 0xffff;

    uint32_t k;
    while (len > 0) {
        k = (len < nmax ? len : nmax);
        len -= k;

        while (k >= 16) {
            DO16(buf);
            buf += 16;
            k -= 16;
        }
        if (k != 0) {
            do {
                s1 += *buf++;
                s2 += s1;
            } while (--k);
        }

        s1 %= base;
        s2 %= base;
    }

    RET(s2 << 16 | s1);
}

/* ( size -- virt ) */
static void
dma_alloc(void)
{
    ucell size = POP();
    ucell addr;
    int ret;

    ret = ofmem_posix_memalign((void *)&addr, size, PAGE_SIZE);

    if (ret) {
        PUSH(0);
    } else {
        PUSH(addr);
    }
}

/* ( virt size cacheable? -- devaddr ) */
static void
dma_map_in(void)
{
    POP();
    POP();
    ucell va = POP();
    PUSH(va);
}

/* ( virt devaddr size -- ) */
static void
dma_sync(void)
{
    ucell size = POP();
    POP();
    ucell virt = POP();

    flush_dcache_range(cell2pointer(virt), cell2pointer(virt + size));
    flush_icache_range(cell2pointer(virt), cell2pointer(virt + size));
}

/* EHCI operational offsets are relative to CAPLENGTH, not the BAR. */
#define EHCI_USBCMD       0x00
#define EHCI_USBSTS       0x04
#define EHCI_CONFIGFLAG   0x40
#define EHCI_PORTSC       0x44
#define EHCI_TIMEOUT_MS   1000

static uint32_t
ehci_read(uint32_t base, unsigned int offset)
{
    return in_le32((volatile unsigned int *)(base + offset));
}

static int
ehci_wait(uint32_t base, unsigned int offset, uint32_t mask,
          uint32_t expected)
{
    unsigned int elapsed;

    for (elapsed = 0; elapsed < EHCI_TIMEOUT_MS; elapsed++) {
        if ((ehci_read(base, offset) & mask) == expected)
            return 0;
        mdelay(1);
    }
    return -1;
}

static void
ehci_dump(uint32_t addr, uint32_t op, unsigned int ports,
          const char *stage)
{
    unsigned int port;

    printk("EHCI %08x %s: CMD=%08x STS=%08x CF=%08x\n",
           addr, stage, ehci_read(op, EHCI_USBCMD),
           ehci_read(op, EHCI_USBSTS), ehci_read(op, EHCI_CONFIGFLAG));
    for (port = 0; port < ports; port++) {
        uint32_t status = ehci_read(op, EHCI_PORTSC + 4 * port);
        printk("  port %u: PORTSC=%08x connected=%u enabled=%u owner=%s\n",
               port + 1, status, status & 1, (status >> 2) & 1,
               (status & (1 << 13)) ? "OHCI" : "EHCI");
    }
}

static void
ehci_disable(uint32_t addr)
{
    uint32_t cap = ehci_read(addr, 0);
    unsigned int length = cap & 0xff;
    unsigned int ports;
    uint32_t op;

    /* Reject an absent BAR or an invalid operational register offset. */
    if (cap == 0xffffffff || length < 0x10 || (length & 3)) {
        printk("EHCI %08x: invalid capability register %08x\n", addr, cap);
        return;
    }
    ports = ehci_read(addr, 4) & 0xf;
    op = addr + length;
    ehci_dump(addr, op, ports, "before reset");

    out_le32((volatile unsigned int *)(op + EHCI_USBCMD),
             ehci_read(op, EHCI_USBCMD) & ~1U);
    if (ehci_wait(op, EHCI_USBSTS, 1 << 12, 1 << 12)) {
        printk("EHCI %08x: halt timed out; skipping reset\n", addr);
        ehci_dump(addr, op, ports, "halt timeout");
        return;
    }

    out_le32((volatile unsigned int *)(op + EHCI_USBCMD),
             ehci_read(op, EHCI_USBCMD) | 2);
    if (ehci_wait(op, EHCI_USBCMD, 2, 0)) {
        printk("EHCI %08x: reset timed out\n", addr);
        ehci_dump(addr, op, ports, "reset timeout");
        return;
    }
    ehci_dump(addr, op, ports, "after reset");
}

/* The generic Forth ms word uses dummy ticks on this platform. */
static void xbox360_boot_wait(void)
{
    mdelay(100);
}

void
arch_of_init(void)
{
    char buf[256];
    const char *stdin_path, *stdout_path, *boot_path;
    char *boot_device;
    ofmem_t *ofmem = ofmem_arch_get_private();
    ucell load_base;

    openbios_init();
    modules_init();
    setup_timers();
    const char *mode_test = xbox360_try_1080p();

    bind_func("xenon-hdmi-audio", xbox360_hdmi_audio);
    bind_func("xenon-boot-1080p?", xbox360_boot_is_1080p);

    bind_func("ppc-dma-alloc", dma_alloc);
    feval("['] ppc-dma-alloc to (dma-alloc)");
    bind_func("ppc-dma-map-in", dma_map_in);
    feval("['] ppc-dma-map-in to (dma-map-in)");
    bind_func("ppc-dma-sync", dma_sync);
    feval("['] ppc-dma-sync to (dma-sync)");

    printk("\n");
    printk("=============================================================\n");
    printk(PROGRAM_NAME " " OPENBIOS_VERSION_STR " [%s]\n",
           OPENBIOS_BUILD_DATE);

    printk("Memory: %dM\n", ofmem->ramsize / 1024 / 1024);

    //
    // Create the device tree info.
    //
    memory_xenon_init();
    cpu_xenon_init();
    printk("%s PVR 0x%X\n", XBOX360_CPU_NAME, mfpvr());

    snprintf(buf, sizeof(buf), "/cpus/%s", XBOX360_CPU_NAME);
    ofmem_register(find_dev("/memory"), find_dev(buf));
    node_methods_init(buf);

    stdin_path = "keyboard";
    stdout_path = "screen";

    /* Setup nvram variables */
    push_str("/options");
    fword("find-device");

    /* No bootorder present, use fw_cfg device if no custom
        boot-device specified */
    fword("boot-device");
    boot_device = pop_fstr_copy();

    if (boot_device && strcmp(boot_device, "disk") == 0) {
        boot_path = "hd";

        snprintf(buf, sizeof(buf),
                    "%s:,\\\\:tbxi "
                    "%s:,\\ppc\\bootinfo.txt "
                    "%s:,%%BOOT",
                    boot_path, boot_path, boot_path);

        push_str(buf);
        fword("encode-string");
        push_str("boot-device");
        fword("property");
    }

    free(boot_device);

    /* Set up other properties */

    push_str("/chosen");
    fword("find-device");

    push_str(xbox360_scanout_info);
    fword("encode-string");
    push_str("xbox360-inherited-scanout");
    fword("property");

    push_str(mode_test);
    fword("encode-string");
    push_str("xbox360-mode-test");
    fword("property");

    if (native_hdmi.version)
        set_property(find_dev("/chosen"), XENON_NATIVE_HDMI_PROPERTY,
                     (char *)&native_hdmi, sizeof(native_hdmi));

    /* PowerPC device-tree cells carry the physical base and byte length. */
    static const uint32_t vblank_surface[] = { XBOX360_VBLANK_BASE, XBOX360_VBLANK_SIZE };
    set_property(find_dev("/chosen"), XBOX360_VBLANK_PROPERTY,
                 (const char *)vblank_surface, sizeof(vblank_surface));

    printk("Xenos inherited scanout: %s; using %ux%u\n", xbox360_scanout_info,
           xbox360_video_width, xbox360_video_height);

    push_str(stdin_path);
    fword("pathres-resolve-aliases");
    push_str("input-device");
    fword("$setenv");

    push_str(stdout_path);
    fword("pathres-resolve-aliases");
    push_str("output-device");
    fword("$setenv");

    fword("activate-tty-interface");

    // Quiesce inherited EHCI state before PCI setup.
    ehci_disable(0xEA003000);
    ehci_disable(0xEA005000);

    // Configure PCI.
    push_str("/");
    fword("find-device");
    feval("\" /\" open-dev to my-self");
    ob_pci_init();
#ifdef CONFIG_DRIVER_USB_EHCI
    ob_usb_init_pending();
#endif
    feval("0 to my-self");

    for (int i = 0; i < 0x10; i++) {
        out_le32((uint32_t*)(0xEA000000 + 0x10 + i * 4), 0);
    }

    // Ack all outstanding interrupts.
    while (in_be64((uint64_t*)(XBOX360_IC_VIRT + 0x50)) != 0x7C);
    out_be64((uint64_t*)(XBOX360_IC_VIRT + 0x68), 0);

    device_end();

    // Bind poweroff and reset words.
    bind_func("ppc32-power-off", xbox360_poweroff);
    feval("['] ppc32-power-off to power-off");
    bind_func("ppc32-reset-all", xbox360_reset_all);
    feval("['] ppc32-reset-all to reset-all");

    /* Implementation of filll word (required by BootX) */
    bind_func("filll", ffilll);

    /* Implementation of adler32 word (required by OS 9, BootX) */
    bind_func("(adler32)", adler32);

    bind_func("platform-boot", boot);
    bind_func("(arch-go)", arch_go);
    bind_func("xbox360-boot-wait", xbox360_boot_wait);

    /* Allocate 8MB memory at load-base */
    fword("load-base");
    load_base = POP();
    ofmem_claim_phys(load_base, 0x800000, 0);
    ofmem_claim_virt(load_base, 0x800000, 0);
    ofmem_map(load_base, load_base, 0x800000, 0);
}
