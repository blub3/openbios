/*
 * Polling EHCI control/bulk driver for Xbox 360.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Scope: directly attached high-speed storage. Low/full-speed root devices
 * are handed to companions. Periodic transfers and hub TTs are not supported.
 * All DMA objects occupy private 128-byte cache lines on Xenon.
 */
#include "config.h"
#include <asm/io.h>
#include <libopenbios/bindings.h>
#include <libopenbios/ofmem.h>
#include "libc/byteorder.h"
#include "drivers/usb.h"
#include "usb.h"
#include "pci.h"
#include "timer.h"

#define CMD 0x00
#define STS 0x04
#define INTR 0x08
#define SEGMENT 0x10
#define ASYNC 0x18
#define CF 0x40
#define PORT 0x44
#define RUN 1
#define RESET 2
#define ASE (1 << 5)
#define HALTED (1 << 12)
#define ASS (1 << 15)
#define ACTIVE (1 << 7)
#define ERRORS 0x7c
#define OWNER (1 << 13)
#define CHANGE 0x2a
#define BUFFER_SIZE 16384

struct ehci_td {
    u32 next, alternate, token, buffer[5], high[5];
    u32 padding[19];
};
struct ehci_qh {
    u32 link, endpoint, caps, current;
    u32 next, alternate, token, buffer[5], high[5];
    u32 padding[15];
};
typedef char ehci_td_size_check[sizeof(struct ehci_td) == 128 ? 1 : -1];
typedef char ehci_qh_size_check[sizeof(struct ehci_qh) == 128 ? 1 : -1];

typedef struct {
    u32 op;
    unsigned int ports;
    int failed;
    int traced;
    struct ehci_qh *qh;
    struct ehci_td *td;
    u8 *buffer;
} ehci_t;

static u32 rd(ehci_t *e, unsigned int offset)
{
    return in_le32((volatile unsigned int *)(e->op + offset));
}
static void wr(ehci_t *e, unsigned int offset, u32 value)
{
    out_le32((volatile unsigned int *)(e->op + offset), value);
}
static int wait_bits(ehci_t *e, unsigned int offset, u32 mask, u32 value)
{
    int i;
    for (i = 0; i < 1000; i++) {
        if ((rd(e, offset) & mask) == value)
            return 0;
        mdelay(1);
    }
    return -1;
}
static void clean(void *p, unsigned int size)
{
    unsigned int i;
    for (i = 0; i < size; i += 128)
        __asm__ volatile("dcbf 0,%0" : : "r"((char *)p + i) : "memory");
    __asm__ volatile("sync" : : : "memory");
}
static void invalidate(void *p, unsigned int size)
{
    /* These private cache lines were flushed before DMA submission and are
     * only read by the CPU until the schedule stops. No dirty CPU data can
     * overwrite a DMA result, so writeback-and-invalidate is safe here too.
     * Avoid the invalidate-only instruction in this Xenon bring-up path.
     */
    clean(p, size);
}
static void *dma_alloc(unsigned int size)
{
    void *p = NULL;
    if (ofmem_posix_memalign(&p, 128, size))
        return NULL;
    memset(p, 0, size);
    return p;
}

/* One qTD on a self-linked asynchronous QH. Return actual byte count.
 * DMA allocations remain pinned if the controller cannot be stopped. */
static int transfer(usbdev_t *dev, int endpoint, int packet, int pid,
                    int toggle, void *data, int size)
{
    ehci_t *e = dev->controller->instance;
    struct ehci_qh *qh = e->qh;
    struct ehci_td *td = e->td;
    u32 address, token = ACTIVE;
    int i, result = -1;
    if (e->failed || size < 0 || size > BUFFER_SIZE || packet <= 0)
        return -1;
    if (!e->traced) printk("EHCI first transfer: preparing DMA\n");
    memset(qh, 0, sizeof(*qh));
    memset(td, 0, sizeof(*td));
    memset(e->buffer, 0, BUFFER_SIZE);
    if (size && pid != 1)
        memcpy(e->buffer, data, size);
    address = virt_to_phys(e->buffer);
    td->next = td->alternate = __cpu_to_le32(1);
    td->token = __cpu_to_le32(((u32)toggle << 31) | ((u32)size << 16) |
                            (3 << 10) | (pid << 8) | ACTIVE);
    td->buffer[0] = __cpu_to_le32(address);
    for (i = 1; i < 5; i++)
        td->buffer[i] = __cpu_to_le32((address & ~4095U) + i * 4096);
    qh->link = __cpu_to_le32(virt_to_phys(qh) | 2);
    qh->endpoint = __cpu_to_le32(dev->address | ((endpoint & 15) << 8) |
                       (2 << 12) | (1 << 14) | (1 << 15) | (packet << 16));
    qh->caps = __cpu_to_le32(1U << 30);
    qh->next = __cpu_to_le32(virt_to_phys(td));
    qh->alternate = __cpu_to_le32(1);
    if (!e->traced) printk("EHCI first transfer: flushing DMA\n");
    clean(e->buffer, BUFFER_SIZE);
    clean(td, sizeof(*td));
    clean(qh, sizeof(*qh));
    if (!e->traced) printk("EHCI first transfer: starting schedule\n");
    wr(e, ASYNC, virt_to_phys(qh));
    wr(e, CMD, rd(e, CMD) | ASE);
    if (wait_bits(e, STS, ASS, ASS))
        goto stop;
    if (!e->traced) printk("EHCI first transfer: polling completion\n");
    for (i = 0; i < 2000; i++) {
        invalidate(td, sizeof(*td));
        token = __le32_to_cpu(td->token);
        if (!(token & ACTIVE))
            break;
        if (rd(e, STS) & (1 << 4)) /* Host system error. */
            break;
        mdelay(1);
    }
    if (!(token & (ACTIVE | ERRORS)))
        result = size - ((token >> 16) & 0x7fff);
stop:
    wr(e, CMD, rd(e, CMD) & ~ASE);
    if (wait_bits(e, STS, ASS, 0)) {
        e->failed = 1;
        wr(e, CMD, rd(e, CMD) & ~RUN);
        wait_bits(e, STS, HALTED, HALTED);
        printk("EHCI: schedule stop timed out; controller disabled\n");
        return -1;
    }
    if (result >= 0 && pid == 1 && result) {
        invalidate(e->buffer, BUFFER_SIZE);
        memcpy(data, e->buffer, result);
    }
    if (result < 0)
        printk("EHCI %08x address %d ep %d: transfer failed token=%08x\n",
               dev->controller->reg_base, dev->address, endpoint, token);
    if (!e->traced) printk("EHCI first transfer: result=%d\n", result);
    e->traced = 1;
    return result;
}

static int control(usbdev_t *dev, direction_t dir, int reqsize, void *req,
                   int size, u8 *data)
{
    int done = 0, toggle = 1;
    int packet = dev->endpoints[0].maxpacketsize;
    if (dev->speed != HIGH_SPEED || reqsize != 8 || size < 0)
        return -1;
    if (transfer(dev, 0, packet, 2, 0, req, 8) != 8)
        return -1;
    while (done < size) {
        int count = MIN(packet, size - done);
        int actual = transfer(dev, 0, packet, dir == IN ? 1 : 0,
                              toggle, data + done, count);
        if (actual < 0 || (dir != IN && actual != count))
            return -1;
        done += actual;
        toggle ^= 1;
        if (actual < count)
            break;
    }
    return transfer(dev, 0, packet, size && dir == IN ? 0 : 1,
                    1, NULL, 0) < 0 ? -1 : 0;
}

static int bulk(endpoint_t *ep, int size, u8 *data, int finalize)
{
    int done = 0;
    if (size < 0 || ep->maxpacketsize != 512)
        return -1;
    do {
        int count = MIN(BUFFER_SIZE, size - done);
        int actual = transfer(ep->dev, ep->endpoint, ep->maxpacketsize,
                              ep->direction == IN ? 1 : 0, !!ep->toggle,
                              data ? data + done : NULL, count);
        int packets;
        if (actual < 0)
            return -1;
        packets = (actual + ep->maxpacketsize - 1) / ep->maxpacketsize;
        if (!actual || (actual < count && !(actual % ep->maxpacketsize)))
            packets++; /* terminating short zero-length packet */
        ep->toggle = (!!ep->toggle) ^ (packets & 1);
        done += actual;
        if (actual < count)
            return 0; /* USB core expects zero on successful short transfers. */
    } while (done < size);
    if (finalize && size && !(size % ep->maxpacketsize) && ep->direction == OUT) {
        if (transfer(ep->dev, ep->endpoint, 512, 0, !!ep->toggle, NULL, 0) < 0)
            return -1;
        ep->toggle = !ep->toggle;
    }
    return 0;
}

static void start(hci_t *c) { ehci_t *e = c->instance; wr(e, CMD, rd(e, CMD) | RUN); }
static void stop(hci_t *c) { ehci_t *e = c->instance; wr(e, CMD, rd(e, CMD) & ~RUN); }
static void reset(hci_t *c)
{
    ehci_t *e = c->instance;
    stop(c);
    if (wait_bits(e, STS, HALTED, HALTED)) { e->failed = 1; return; }
    wr(e, CMD, RESET);
    if (wait_bits(e, CMD, RESET, 0)) e->failed = 1;
}
static void shutdown(hci_t *c)
{
    ehci_t *e = c->instance;
    int i;
    stop(c);
    if (wait_bits(e, STS, HALTED, HALTED))
        panic("EHCI failed to halt before OS handoff");
    for (i = 1; i < 128; i++)
        if (c->devices[i]) usb_detach_device(c, i);
    wr(e, CF, 0);
    detach_controller(c);
    free(e->qh); free(e->td); free(e->buffer); free(e); free(c);
}
static int is_bulk_storage(usbdev_t *dev)
{
    configuration_descriptor_t *cd = (void *)dev->configuration;
    interface_descriptor_t *iface = (void *)((u8 *)cd + cd->bLength);
    return iface->bInterfaceClass == 8 && iface->bInterfaceProtocol == 0x50;
}
static int storage_only(usbdev_t *dev)
{
    if (is_bulk_storage(dev))
        return 0;
    device_descriptor_t *dd = (void *)dev->descriptor;
    configuration_descriptor_t *cd = (void *)dev->configuration;
    interface_descriptor_t *iface = (void *)((u8 *)cd + cd->bLength);
    printk("EHCI address %d: %04x:%04x class=%02x interface=%02x/%02x/%02x; needs OHCI\n",
           dev->address, __le16_to_cpu(dd->idVendor), __le16_to_cpu(dd->idProduct),
           dd->bDeviceClass, iface->bInterfaceClass, iface->bInterfaceSubClass,
           iface->bInterfaceProtocol);
    return -1;
}
static void port_write(ehci_t *e, int port, u32 clear, u32 set)
{
    unsigned int reg = PORT + 4 * port;
    wr(e, reg, (rd(e, reg) & ~(CHANGE | clear)) | set);
}
static void release_unsupported(hci_t *c, int port, int address)
{
    ehci_t *e = c->instance;
    usbdev_t *dev = address >= 0 ? c->devices[address] : NULL;
    /* A successful address assignment does not mean we have a class driver.
     * Unsupported high-speed devices (including hubs) must be reset and
     * enumerated by the companion, rather than left stranded on EHCI.
     * Do not reroute a controller with a failed/still-active DMA schedule.
     */
    if (!dev || e->failed || is_bulk_storage(dev))
        return;
    usb_detach_device(c, address);
    port_write(e, port, 1 << 2, OWNER);
    mdelay(100);
    printk("EHCI port %d: unsupported device handed to OHCI\n", port + 1);
}
int ob_usb_ehci_init(const char *path, uint32_t pci)
{
    u32 bar = pci_config_read32(pci, PCI_BASE_ADDR_0) & ~15U;
    u32 cap, params;
    ehci_t *e;
    hci_t *c;
    int port;
    (void)path;
    pci_config_write16(pci, PCI_COMMAND, pci_config_read16(pci, PCI_COMMAND) |
                       PCI_COMMAND_BUS_MASTER | PCI_COMMAND_MEMORY);
    cap = in_le32((volatile unsigned int *)bar);
    if (cap == ~0U || (cap & 0xff) < 0x10 || (cap & 3)) return -1;
    params = in_le32((volatile unsigned int *)(bar + 4));
    e = malloc(sizeof(*e));
    if (!e) return -1;
    memset(e, 0, sizeof(*e));
    e->op = bar + (cap & 0xff);
    e->ports = params & 15;
    e->qh = dma_alloc(sizeof(*e->qh));
    e->td = dma_alloc(sizeof(*e->td));
    e->buffer = dma_alloc(BUFFER_SIZE);
    if (!e->qh || !e->td || !e->buffer) {
        free(e->qh); free(e->td); free(e->buffer); free(e); return -1;
    }
    c = new_controller();
    if (!c) { free(e->qh); free(e->td); free(e->buffer); free(e); return -1; }
    c->instance = e; c->reg_base = bar; c->type = EHCI;
    c->start = start; c->stop = stop; c->reset = reset; c->init = start;
    c->shutdown = shutdown; c->control = control; c->bulk = bulk;
    c->set_address = generic_set_address; c->finish_device_config = storage_only;
    reset(c);
    if (e->failed) { shutdown(c); return -1; }
    wr(e, INTR, 0);
    wr(e, SEGMENT, 0);
    wr(e, CMD, (8 << 16) | RUN);
    if (wait_bits(e, STS, HALTED, 0)) { shutdown(c); return -1; }
    wr(e, CF, 1);
    for (port = 0; port < e->ports; port++)
        if (params & (1 << 4)) port_write(e, port, 0, 1 << 12);
    mdelay(100);
    printk("EHCI %08x: %u ports, direct high-speed storage enabled\n", bar, e->ports);
    for (port = 0; port < e->ports; port++) {
        u32 status = rd(e, PORT + 4 * port);
        if (!(status & 1) || (status & OWNER)) continue;
        if ((status & (3 << 10)) == (1 << 10)) {
            port_write(e, port, 0, OWNER);
            continue;
        }
        port_write(e, port, 1 << 2, 1 << 8);
        mdelay(50);
        port_write(e, port, 1 << 8, 0);
        if (wait_bits(e, PORT + 4 * port, 1 << 8, 0)) {
            printk("EHCI port %d: reset timeout\n", port + 1);
            continue;
        }
        mdelay(20);
        status = rd(e, PORT + 4 * port);
        if (!(status & 1)) continue;
        if (!(status & 4)) {
            port_write(e, port, 0, OWNER);
            printk("EHCI port %d: handed to OHCI\n", port + 1);
            continue;
        }
        printk("EHCI port %d: high-speed device\n", port + 1);
        int address = usb_attach_device(c, 0, port + 1, HIGH_SPEED);
        if (address < 0)
            printk("EHCI port %d: enumeration failed\n", port + 1);
        else
            release_unsupported(c, port, address);
    }
    return 0;
}
