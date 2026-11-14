# Xbox 360 USB storage and EHCI bring-up

## Implemented scope

The Xbox configuration enables CONFIG_DRIVER_USB_EHCI. This experimental
polling driver supports control and bulk transfers for directly connected
high-speed Bulk-Only Transport storage. EHCI initializes before OHCI so that
high-speed devices remain on EHCI and low/full-speed root-port devices are
released to companions. Connect the keyboard directly to the Xbox.

EHCI hub/split transactions, periodic/interrupt transfers, UAS and hotplug
are not implemented. Successfully enumerated devices outside the supported
Bulk-Only storage class are detached from EHCI and their individual ports
handed to OHCI. This allows the existing OHCI hub/HID drivers to try them at
full/low speed, including keyboards behind a built-in hub. Devices behind
such a hub also lose high-speed operation. Direct supported storage stays
on EHCI. This is not complete USB 2.0 class support.

The driver is a new implementation. The libpayload coreboot 4.8 ehci.c was
consulted during investigation, but not imported: its native descriptor
access would need extensive changes on big-endian Xenon.
Reference: https://github.com/coreboot/coreboot/blob/4.8/payloads/libpayload/drivers/usb/ehci.c

## Disk identification

Disk unit addresses use the USB device address instead of a constant zero.
Each disk receives ud0, ud1, ...; ud retains the first disk for compatibility.
Addresses and alias ordering can change when devices or controllers change.
Nodes are created under their own controller, including deferred enumeration.
Each disk prints its SCSI vendor/product, capacity in MiB, sector size,
negotiated link speed and controller type. Device-tree properties include
model, usb-address and usb-speed-kbps. Link speed is not measured throughput.

Mac OS X mkext lookup tries ud and the numbered aliases, partitions 0–9,
so the boot stick need not be the first enumerated disk. READ CAPACITY errors
are no longer replaced with a fabricated 2 TB capacity; disks requiring
READ CAPACITY(16) are explicitly rejected for now.

## DMA and errors

MMIO and all QH/qTD fields use explicit little-endian access. DMA objects and
the bounce buffer occupy private 128-byte Xenon cache lines. The CPU writes
back and invalidates before submission, invalidates while polling and before
copying input data. Each qTD covers at most 16 KiB within five 4 KiB pages.
The asynchronous schedule is stopped before descriptors can be reused.
A failed schedule stop disables the controller and retains its DMA memory.
Shutdown requires the controller to halt before memory is freed or OS handoff
continues. All register/transfer polling loops have finite bounds.

## Validation and hardware test

Build using the repository builder container:

    ./config/scripts/switch-arch xbox360-ppc
    make -j2

Host simulation test (requires a C compiler and sanitizer runtime):

    python3 tests/ehci-transfer-test.py
    python3 tests/usb-keyboard-init-test.py
    python3 tests/ehci-handoff-test.py

This exercises the actual transfer/control/bulk functions with simulated
MMIO and DMA, including endian conversion, page pointers, chunking, short
packets, toggles, control stages, stalls and timeout cleanup. It does not
validate physical DMA, Xenon cache behavior, root-port handoff or real devices.
The keyboard test runs the production root-hub initialization and keyboard
registration functions with mocked ports and device-tree services. It checks
startup scanning without change events and the exact alias and parent node.

1. Keep a known-working ELF available for recovery.
2. Connect the boot stick, SSD and keyboard directly before power-on.
3. Boot using the known-working XeLL launch route; record the startup screen.
4. Expect `EHCI ... high-speed device`, `USB storage: ... 480 Mbit/s (high)`
   and separate paths/aliases for each disk. Run `show-devs` to inspect paths.
5. Check model/capacity against the actual stick and SSD. Verify keyboard input.
6. Validate known file reads and repeat them before attempting macOS boot.
7. Repeat front/rear ports and with only the boot stick. Capture any timeout.

Set CONFIG_DRIVER_USB_EHCI to false in the Xbox config and rebuild for the
OHCI-only path, retaining disk naming/identification and handoff diagnostics.
The old XeLL ELF load-address limit remains a separate issue; this change
does not relocate OpenBIOS. macOS root-disk drivers need separate validation.

## Baseline hardware evidence

The diagnostic build tested on Trinity on November 12, 2026 showed both
controllers changing CONFIGFLAG from 1 to 0 on reset with all ports owned by
OHCI. Two disks and a keyboard appeared with two sticks, and again with the
boot stick plus SSD. These observations establish enumeration through OHCI,
not verified data reads or EHCI functionality.

The next hardware test enumerated the SanDisk stick (29358 MiB) and Intenso
SSD (114473 MiB) on EHCI at 480 Mbit/s, with distinct disk@1 and disk@2 paths.
The previous exception during the first transfer no longer occurred, but
the console reported that the keyboard was not found after a port handoff.
The follow-up build explicitly scans existing OHCI root-port connections at
startup and constructs the keyboard alias from its completed device node,
under its own controller. The follow-up hardware test still reported no
keyboard. Diagnostic v2 therefore prints the queued controller startup order,
OHCI PCI command/BAR, inherited and operational registers, root-port count,
each initial port status and enumerated device/interface classes. Its banner
is `USB startup diagnostic v2`; the general build timestamp can remain stale
in incremental builds. At that stage keyboard recovery remained unresolved (later resolved below).

Diagnostic v2 confirmed that both OHCI controllers initialize and that the
front controller enumerates a vendor-specific Microsoft device. No HID
keyboard appeared, while two unsupported high-speed devices remained on
EHCI. The user identified a wired aluminum Apple keyboard, which may expose
a built-in hub. Build v3 adds per-port OHCI fallback for unsupported classes
and prints their VID/PID and class to confirm this hypothesis on hardware.
Host tests verify that supported storage remains on EHCI, unsupported classes
are released, neighboring ports are untouched, and failed DMA prevents
handoff. The banner is `USB startup v3 (OHCI class fallback)`.


## SSD autoboot

Subsequent hardware testing confirmed keyboard input, distinct EHCI disk paths,
and booting the cloned Tiger 10.4.11 SSD to the desktop, including networking.

The Xbox build now installs an automatic boot command at startup. After console
initialization, it waits five seconds; any key cancels to the OpenBIOS prompt.
It then executes the tested explicit BootX path:

```
boot ud1:,\System\Library\CoreServices\BootX
```

The `mac-boot` command runs the same path without requiring backslashes to be
typed. Keep the SanDisk boot stick and Intenso SSD in their tested USB ports:
this configuration expects the SSD at `ud1`. Keep `Xbox360.mkext` on the boot
stick. A returned boot or caught Forth exception leaves the console available
instead of retrying indefinitely. The delay uses the Xenon timebase through a
C binding, since the generic Forth timing implementation is not suitable here.

The existing stick's `kboot.conf` already selects `openbios-xbox360.elf` with
`timeout=0`. Launch XeLL from Aurora with both drives connected. XeLL loads
OpenBIOS, which performs the SSD boot. No persistent Xbox NVRAM is required.
Restore `openbios-xbox360.before-autoboot.elf.bak` to
`openbios-xbox360.elf` on the stick to recover the prior manual-boot build.

The autoboot build compiled successfully. Host Forth checks covered the full
countdown, immediate and mid-countdown cancellation, absent console input,
and a caught boot exception. The user subsequently confirmed automatic SSD boot through XeLL launched from
Aurora. Independent measurement of the countdown duration was not reported.
See [hardware-results.md](hardware-results.md) for the consolidated results and
unresolved installation/networking issues.
