#!/usr/bin/env python3
"""Host regression checks for startup scanning and keyboard alias registration.
Runs the production init functions with mocked ports and device-tree services.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
rh = (root / 'drivers/usbohci_rh.c').read_text()
rh = rh[rh.index('void\nohci_rh_init'):]
hid = (root / 'drivers/usbhid.c').read_text()
hid = hid[hid.index('static void usb_hid_add_keyboard'):hid.index('\nvoid\nusb_hid_init')]
preamble = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int phandle_t;
typedef struct { int node; } controller_t;
typedef struct device {
    controller_t *controller;
    void *data;
    int address, hub, port, speed;
    void (*poll)(struct device *), (*destroy)(struct device *);
} usbdev_t;
typedef struct { int numports; int *port; } rh_inst_t;
#define RH_INST(d) ((rh_inst_t *)(d)->data)
#define OHCI_INST(c) (c)
#define READ_OPREG(c,r) 5
#define NumberDownstreamPortsMask 255
#define LOW_SPEED 1
static int mock_print(const char *fmt, ...) { (void)fmt; return 0; }
#define printk mock_print
#define usb_debug(...) ((void)0)
static int scanned, connected, attached;
static void ohci_rh_destroy(usbdev_t *d) { (void)d; }
static void ohci_rh_poll(usbdev_t *d) { (void)d; }
static void ohci_rh_scanport(usbdev_t *d, int port) {
    assert(d->address == 0 && d->hub == -1 && d->port == -1);
    assert(RH_INST(d)->port[port] == -1);
    scanned |= 1 << port;
    if (connected & (1 << port)) {
        RH_INST(d)->port[port] = port + 1;
        attached |= 1 << port;
    }
}
static int current = 99, finished, parent, unit;
static char alias[128];
static phandle_t get_cur_dev(void) { return current; }
static void activate_dev(phandle_t n) { current = n; }
static void fword(const char *s) {
    if (!strcmp(s, "new-device")) { parent = current; current = 42; }
    if (!strcmp(s, "finish-device")) { finished = 1; current = parent; }
}
static void push_str(const char *s) { (void)s; }
static void set_int_property(int n, const char *key, int val) {
    assert(n == 42 && !strcmp(key, "reg")); unit = val;
}
#define BIND_NODE_METHODS(n,m) assert((n) == 42)
static char *get_path_from_ph(int n) {
    assert(finished && n == 42 && parent == 5 && unit == 2);
    return strdup("/pci/usb@5/keyboard@2");
}
static phandle_t find_dev(const char *s) { assert(!strcmp(s,"/aliases")); return 7; }
static void set_property(int n, const char *key, const char *val, size_t len) {
    assert(n == 7 && !strcmp(key,"keyboard") && len < sizeof(alias));
    memcpy(alias, val, len);
}
'''
test = r'''
int main(void) {
    controller_t c = { .node = 5 };
    /* Existing connections must be scanned even without any change event. */
    for (connected = 0; connected < 32; connected++) {
        usbdev_t d = { .controller = &c, .address = -1 };
        scanned = attached = 0;
        ohci_rh_init(&d);
        assert(scanned == 31 && attached == connected);
        free(RH_INST(&d)->port); free(d.data);
    }
    usbdev_t keyboard = { .controller = &c, .address = 2, .speed = LOW_SPEED };
    usb_hid_add_keyboard(&keyboard);
    assert(current == 99);
    assert(!strcmp(alias, "/pci/usb@5/keyboard@2"));
    puts("USB keyboard init tests passed: existing connections, exact alias, controller parent, restored context");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(preamble + rh + hid + test)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-g', str(path / 'test.c'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
