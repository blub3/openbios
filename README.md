# OpenBIOS for OSX360

This experimental fork of [osx360/openbios](https://github.com/osx360/openbios) extends the original Xbox 360 port of [OpenBIOS](https://github.com/openbios/openbios), the IEEE 1275 Open Firmware implementation. The upstream [OSX360 project](https://github.com/osx360) provides the Xbox 360 firmware port and Mac OS X boot support. This fork adds USB 2.0 storage, Tiger SSD autoboot and firmware handoffs for native HDMI and Xenos GPU tests.

## What's working

| Area | Verified result |
| --- | --- |
| USB 2.0 storage | On an Xbox 360 Trinity, a directly attached SanDisk boot stick and an Intenso 120 GB SSD in a USB enclosure negotiated EHCI at 480 Mbit/s and received distinct disk paths. The figure is the negotiated link speed. |
| Keyboard fallback | The tested wired Apple keyboard worked through OHCI fallback while the two drives used EHCI. |
| Tiger SSD autoboot | Aurora → XeLL → OpenBIOS reached the desktop from an SSD cloned from a Power Mac G5 running Tiger 10.4.11 (8S165). A five-second keyboard cancellation window and a `mac-boot` command are available at the OpenBIOS stage. |
| Native HDMI | OpenBIOS supplies the Slim's captured 720p timing and the native 1080p timing to Tiger. With the companion drivers, the TV confirmed both modes and audio played in both. |
| Xenos scanout | Separate firmware builds reserve scanout storage for double buffering and queued triple buffering. The driver tests verified ordered VBlank flips, with zero dropped or out-of-order frames in the queued test. |
| WindowServer test profiles | `WINAUTO.ELF` and `WINOVR.ELF` cold-booted Tiger with their matching driver archives. The overlapping profile passed two unarmed moves with eight ordered GPU fences per move and complete clipped and on-screen pixel checks. Quartz Extreme remains disabled. [Firmware prerelease](https://github.com/blub3/openbios/releases/tag/gpu-fw-windowserver) |
| SMP groundwork | XeLL enters the image with threads 1–5 at offset `0x60`, and this firmware parks them there in a `b .` loop. Because OpenBIOS is linked at `0x07E00000`, outside Tiger's memory, the five threads stay parked and reachable after boot. The OSX360 drivers' SMP work turns this loop into a release mailbox and runs Tiger on two hardware threads with no firmware change. Measurements showed XeLL leaves all six threads with identical HID, LPCR and HRMOR settings and synchronized time bases. [SMP design](https://github.com/blub3/osx360-drivers/blob/claude/smp/docs/smp-design.md) |

The Tiger DVD installer can see and format the USB SSD. Direct installation from DVD to that SSD remains open. The EHCI path is scoped to directly attached Bulk-Only Transport devices. EHCI hub transactions, periodic transfers, UAS and hotplug are outside the implemented path. [Hardware results](Documentation/xbox360/hardware-results.md) · [USB 2.0 implementation notes](Documentation/xbox360/usb2-bringup.md)

## Branches and firmware tags

- [`codex/usb2-ssd-boot`](https://github.com/blub3/openbios/tree/codex/usb2-ssd-boot) is the default branch. It includes the native HDMI timing handoff and uses the standard `Xbox360.mkext` name.
- [`gpu/quartz-extreme`](https://github.com/blub3/openbios/tree/gpu/quartz-extreme) records the firmware sources and archive names used by the GPU test profiles. Quartz Extreme status is tracked in the companion drivers repository. [Firmware profile guide](https://github.com/blub3/openbios/blob/gpu/quartz-extreme/Documentation/xbox360-gpu-profiles.md) · [Driver milestone tracker](https://github.com/blub3/osx360-drivers/blob/gpu/quartz-extreme/docs/gpu-milestones.md)
- [`claude/smp`](https://github.com/blub3/openbios/tree/claude/smp) is the base for SMP firmware work. Two-thread SMP currently needs no firmware change.

| Firmware source tag | Use |
| --- | --- |
| [`gpu-fw-native-hdmi`](https://github.com/blub3/openbios/tree/gpu-fw-native-hdmi) | Native HDMI test and the base ELF for archive-name variants, including `WINTILE.ELF`, `WINAUTO.ELF` and `WINOVR.ELF`. |
| [`gpu-fw-vblank-reserve`](https://github.com/blub3/openbios/tree/gpu-fw-vblank-reserve) | Double-buffered scanout retry. |
| [`gpu-fw-vblank-queue`](https://github.com/blub3/openbios/tree/gpu-fw-vblank-queue) | Triple-buffered scanout with queued VBlank flips. |

## Build and test

From the repository root, build the Xbox 360 PowerPC target with the project builder image and Docker.

```sh
docker run --rm -v "$PWD:/src" -w /src ghcr.io/openbios/openbios-builder:master sh -c './config/scripts/switch-arch xbox360-ppc && make -j2'
```

The build writes `obj-ppc/openbios-xbox360.elf`. Record the source commit and builder image digest alongside the ELF checksum for repeatable builds. [General OpenBIOS build notes](README)

Run the host simulations with Python 3 and a C compiler with sanitizer support. These exercise selected firmware paths using mocked hardware. Real DMA, cache behavior and OS handoff are covered by console testing.

```sh
python3 tests/ehci-transfer-test.py
python3 tests/usb-keyboard-init-test.py
python3 tests/ehci-handoff-test.py
```

## Boot the tested USB layout

On the tested setup, XeLL launches the ELF from a FAT32 boot stick. Place `openbios-xbox360.elf` and the matching `Xbox360.mkext` at its root. Use a separate SSD with an existing Tiger system on an Apple Partition Map disk. Keep a known-working XeLL entry available when testing firmware changes. [Companion OSX360 drivers](https://github.com/blub3/osx360-drivers) · [Experimental ELF and mkext pairings](https://github.com/blub3/osx360-drivers/blob/gpu/quartz-extreme/docs/gpu-boot-profiles.md)

The simple `kboot.conf` below uses the boot stick at `uda0`.

```text
#KBOOTCONFIG
default=openbios
timeout=0
openbios="uda0:/openbios-xbox360.elf"
```

The OpenBIOS countdown is separate from the XeLL menu. Press any key during the OpenBIOS countdown to reach its prompt. The tested boot stick enumerates first and the SSD appears at `ud1`. Verify the aliases if you change ports or devices. With the tested disk order, `mac-boot` runs the following command.

```text
boot ud1:,\System\Library\CoreServices\BootX
```

The default is defined in [`xbox360.fs`](arch/ppc/xbox360/xbox360.fs) and is set on each startup because this target's NVRAM is RAM-only.

## Credits and license

Credit goes to the [OpenBIOS contributors](https://github.com/openbios/openbios) for the firmware and the [OSX360 contributors](https://github.com/osx360/openbios) for the Xbox 360 port and Mac OS X boot support. The original Git history and copyright notices are retained. The top-level [COPYING](COPYING) contains GPL version 2. Source headers and component licenses also apply. [Credits and license inventory](Documentation/xbox360/credits-and-licensing.md) · [Fork change record](Documentation/xbox360/changes.md) The SMP findings on this firmware were made by [blub3](https://github.com/blub3) together with Claude.
