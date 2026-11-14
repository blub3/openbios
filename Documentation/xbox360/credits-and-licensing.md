# Credits and license inventory

Reviewed for publication preparation on 2026-11-14. This is a scoped inventory
of the project license files and the code involved in this work, not an audit
of every historical file or external binary.

## Upstream lineage

- Immediate upstream: [osx360/openbios](https://github.com/osx360/openbios).
- Original project: [openbios/openbios](https://github.com/openbios/openbios).
- Local development baseline:
  [`d6f664856e184e4f939606cc364d69ada678c71e`](https://github.com/osx360/openbios/commit/d6f664856e184e4f939606cc364d69ada678c71e).

The original Git history is retained. The original README credits Stefan
Reinauer, Samuel Rydh and Patrick Mauritz for OpenBIOS and Jens Axboe for the
IDE driver. The history records John Davis's Xbox 360 target (`8a3d5cf`),
Mac OS X module (`305c23c`) and poweroff/reset support (`da738ab`). Needle's
PCI enumeration fix (`03d7637`) is also part of the inherited baseline.
These are examples, not an exhaustive contributor list; Git history and
source headers remain the authoritative attribution record.

The existing OHCI driver credits BALATON Zoltan's OpenBIOS port and Patrick
Georgi's libpayload work. Preserve these notices. The new EHCI implementation
was developed for this fork; investigation consulted
[coreboot 4.8 libpayload ehci.c](https://github.com/coreboot/coreboot/blob/4.8/payloads/libpayload/drivers/usb/ehci.c),
which was not imported as a source file. See the implementation notes for scope.

This fork's contributor supplied the hardware, repeated tests, screenshots
and boot verification. Implementation and documentation were developed with
Codex assistance. This does not imply endorsement by OpenBIOS, osx360,
coreboot, Apple or Microsoft.

## License files and notices

| Material | Existing license evidence | Publication treatment |
| --- | --- | --- |
| OpenBIOS project | Root `COPYING`: GNU GPL version 2 | Retain unchanged; consult source headers for version options. |
| Xbox platform files | Headers include GPL version 2 or later and original copyright notices | Retain notices and record modifications. |
| New `drivers/usbehci.c` | `SPDX-License-Identifier: GPL-2.0-or-later` | Publish source under that stated license. |
| libpayload-derived OHCI code | `drivers/usbohci.c` contains a three-clause BSD-style notice | Retain full notice; reproduce it in binary distribution materials. |
| FatFs | `fs/fatfs/LICENSE.txt` and file headers, crediting ChaN | Retain source notices and disclaimer. |
| Other bundled material | `Documentation/kernel/COPYING`, `utils/devbios/COPYING`, and per-file headers | Preserve these files and their applicable notices. |

Do not replace existing notices with a new blanket MIT/BSD license or claim
ownership of the original port. For a binary release, include the applicable
license/notices and corresponding source for the exact build, including build
scripts and modifications. A source archive from a different commit is not
an adequate description of that build.

No Mac OS X media, installed system image, console keys, private SSH keys or
unreviewed third-party binary packages belong in this publication. The
`Xbox360.mkext` used on the test system is obtained separately; its presence
on the user's USB stick does not establish permission to redistribute it here.
