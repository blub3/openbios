#!/usr/bin/env python3
"""Exercise production class filtering and per-port OHCI fallback with mock MMIO."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'drivers/usbehci.c').read_text()
source = source[source.index('static int is_bulk_storage'):source.index('int ob_usb_ehci_init')]
preamble = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef struct { u8 bLength; } configuration_descriptor_t;
typedef struct { u8 bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol; } interface_descriptor_t;
typedef struct { uint16_t idVendor, idProduct; u8 bDeviceClass; } device_descriptor_t;
typedef struct { void *configuration, *descriptor; int address; } usbdev_t;
typedef struct { int failed; } ehci_t;
typedef struct { void *instance; usbdev_t *devices[128]; } hci_t;
#define __le16_to_cpu(x) (x)
#define PORT 0x44
#define OWNER (1 << 13)
#define CHANGE 0x2a
static int mock_print(const char *fmt, ...) { (void)fmt; return 0; }
#define printk mock_print
static u32 ports[5];
static int writes, detached, delay;
static u32 rd(ehci_t *e, unsigned int reg) { (void)e; return ports[(reg-PORT)/4]; }
static void wr(ehci_t *e, unsigned int reg, u32 val) {
    (void)e;
    assert((val & CHANGE) == 0); /* Must not acknowledge unrelated W1C flags. */
    writes++; ports[(reg-PORT)/4] = val;
}
static void mdelay(int n) { delay += n; }
static void usb_detach_device(hci_t *c, int address) {
    assert(c->devices[address]);
    detached++; c->devices[address] = NULL;
}
'''
test = r'''
int main(void) {
    struct { configuration_descriptor_t cd; interface_descriptor_t iface; } cfg = { {1}, {8,6,0x50} };
    device_descriptor_t dd = {0x5ac,0x1006,9};
    usbdev_t dev = { &cfg, &dd, 1 };
    ehci_t e = {0};
    hci_t c = { .instance = &e, .devices = { [1] = &dev } };
    ports[2] = 0x1005 | CHANGE; ports[3] = 0x1005;
    assert(storage_only(&dev) == 0);
    release_unsupported(&c, 2, 1);
    assert(!writes && !detached && c.devices[1] == &dev);
    /* Hub, HID, vendor-specific and non-BOT storage all need a companion. */
    int classes[] = {9, 3, 0xff, 8};
    for (int i=0; i<4; i++) {
        cfg.iface.bInterfaceClass = classes[i]; cfg.iface.bInterfaceProtocol = 0;
        c.devices[1] = &dev; ports[2] = 0x1005 | CHANGE;
        assert(storage_only(&dev) != 0);
        release_unsupported(&c, 2, 1);
        assert(!c.devices[1] && (ports[2] & OWNER) && !(ports[2] & 4));
        assert(ports[2] & 0x1000); /* Preserve port power. */
        assert(ports[3] == 0x1005); /* Do not reroute adjacent storage. */
    }
    assert(writes == 4 && detached == 4 && delay == 400);
    c.devices[1] = &dev; e.failed = 1;
    release_unsupported(&c, 2, 1);
    assert(writes == 4 && c.devices[1] == &dev);
    e.failed = 0;
    release_unsupported(&c, 2, -1);
    c.devices[1] = NULL; release_unsupported(&c, 2, 1);
    assert(writes == 4);
    puts("EHCI handoff tests passed: storage retained, unsupported classes released, port isolation, DMA failure guard");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path/'test.c').write_text(preamble + source + test)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-g', str(path/'test.c'),
                    '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
