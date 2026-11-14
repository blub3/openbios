# OpenBIOS for Xbox 360: experimental EHCI and SSD boot

An unofficial development fork of **[osx360/openbios](https://github.com/osx360/openbios)**,
itself based on [OpenBIOS](https://github.com/openbios/openbios).
The Xbox 360 port and Mac OS X boot support were created upstream; this work
extends their USB storage and boot paths. It is not an official osx360 release.

## What this fork adds

- Experimental EHCI control/bulk transfers for directly connected high-speed
  USB Bulk-Only Transport storage, with explicit Xenon endian/cache handling.
- Per-port OHCI fallback for unsupported device classes, preserving keyboard
  support in the tested wired Apple keyboard configuration.
- Distinct USB disk paths and numbered aliases, model/capacity/link-speed
  output, and driver-package lookup across USB disk aliases.
- Automatic boot through XeLL → OpenBIOS → Mac OS X on the tested SSD,
  with a five-second keyboard cancellation window and a `mac-boot` command.
- Host simulations covering transfer handling, handoff and keyboard startup.

## Hardware-tested status

On the contributor's Xbox 360 Trinity, a SanDisk USB stick and an Intenso
120 GB SATA SSD in a USB enclosure enumerate on EHCI at **480 Mbit/s**.
That is negotiated link speed, not a measured transfer rate. The wired
aluminum Apple keyboard works through OHCI. Distinct `disk@1` and `disk@2`
nodes appear under the EHCI controller.

Mac OS X Tiger **10.4.11 (8S165)**, cloned from a Power Mac G5 with Carbon Copy
Cloner, reaches the desktop from the USB SSD. Automatic boot when launching
XeLL from Aurora has also been confirmed by the hardware tester.

The Tiger DVD installer starts and can see/format the SSD, but rejected it
as an installation target. Direct installation from DVD is **not solved**.
Networking and SSH have worked, but intermittent connectivity remains
unresolved; a network-driver crash has not been established.

See [hardware results](Documentation/xbox360/hardware-results.md) for the
scope of these observations and [implementation notes](Documentation/xbox360/usb2-bringup.md)
for limitations. There is no EHCI hub/split-transaction, periodic-transfer,
UAS or hotplug implementation. Other Xbox revisions and USB devices need testing.

## Build and test

The existing project workflow uses `ghcr.io/openbios/openbios-builder:master`.
From the repository root, with Docker available:

```sh
docker run --rm -v "$PWD:/src" -w /src ghcr.io/openbios/openbios-builder:master sh -c './config/scripts/switch-arch xbox360-ppc && make -j2'
```

The Xbox ELF is generated at `obj-ppc/openbios-xbox360.elf`.
The builder tag is mutable; record its image digest and the source commit
when producing a reproducible release.

Run the host simulations with Python 3, a C compiler and sanitizer support:

```sh
python3 tests/ehci-transfer-test.py
python3 tests/usb-keyboard-init-test.py
python3 tests/ehci-handoff-test.py
```

These simulations do not replace testing real DMA/cache behavior or OS handoff.
General upstream build documentation remains in [README](README).

## Boot configuration

Keep a known-working ELF for recovery. The current autoboot default is specific
to the tested layout: boot stick first, SSD at `ud1`. Enumeration order can
change with port/device changes. Verify aliases before relying on this default.

The FAT32 boot stick's `kboot.conf` uses:

```text
#KBOOTCONFIG
default=openbios
timeout=0
openbios="uda0:/openbios-xbox360.elf"
```

Keep the required `Xbox360.mkext` on the boot stick. Obtain the driver package
separately from the [osx360 drivers project](https://github.com/osx360/osx360-drivers).
Mac OS X installation media, cloned volumes and driver binaries are not supplied
by this source repository.

At the OpenBIOS countdown, any key cancels autoboot. At its prompt, `mac-boot`
uses the explicit path below, avoiding manual backslash entry:

```text
boot ud1:,\System\Library\CoreServices\BootX
```

This default lives in `arch/ppc/xbox360/xbox360.fs`; it is installed on each
startup because this target's NVRAM is RAM-only. XeLL load-address restrictions
are a separate compatibility issue and have not been fixed here.

## Credit and licensing

Please credit and link **[osx360/openbios](https://github.com/osx360/openbios)**
when sharing this work. The original Git history and copyright notices are
retained. See [credits and license inventory](Documentation/xbox360/credits-and-licensing.md).

The top-level [COPYING](COPYING) contains GPL version 2. Individual source
headers and component licenses remain applicable; this fork does not relicense
the whole tree. Our additions and hardware integration are documented in
[the change record](Documentation/xbox360/changes.md).

[Publishing guide](Documentation/xbox360/publishing.md) describes how to publish
as a GitHub fork while retaining attribution and excluding private test material.
