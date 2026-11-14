# Hardware validation record

Recorded 2026-11-14 from the contributor's test reports and console output.
This is a record of one tested setup, not a compatibility guarantee.

## Setup

- Xbox 360 Trinity; Peer Pressure environment reported during initial bring-up.
- XeLL launch, later confirmed through Aurora.
- FAT32 SanDisk USB boot stick: reported 29358 MiB.
- Intenso SSD SATA III in a USB enclosure: reported 114473 MiB, 512-byte sectors.
- Wired aluminum Apple keyboard, connected to a front Xbox USB port.
- Tiger 10.4.11, build 8S165, cloned from a Power Mac G5 with Carbon Copy Cloner.
- SSD uses Apple Partition Map and an HFS+ system volume.

## Results

| Test | Observed result | What remains unproven |
| --- | --- | --- |
| USB storage identification | Both storage devices identified at EHCI 480 Mbit/s with capacities and models | Sustained throughput, prolonged stress and broad enclosure compatibility |
| Device-tree paths | Separate `usb@4,1/disk@1` and `usb@4,1/disk@2` | Stable ordering across different ports/device combinations |
| Keyboard | Input works; `usb@5/keyboard@2` appears after OHCI fallback | Other Apple keyboard/hub models and hotplug |
| DVD boot | Tiger installer starts; the boot log reports XNU 8.6.0 | Successful direct DVD installation to USB SSD |
| Installer storage | SSD visible and formattable | Installer rejected target with “Mac OS X cannot start up from this volume”; cause not established |
| Cloned system | Tiger 10.4.11 reaches desktop from USB SSD | Long-term system stability and all applications/devices |
| Automatic boot | User confirmed Aurora → XeLL → OpenBIOS → Tiger works | Other launch environments and disk layouts |
| Ethernet/SSH | Connectivity and SSH succeeded initially; later intermittent failures | Driver crash is a hypothesis, not confirmed; further logs and traffic tests needed |

## Interpretation

The successful desktop boot establishes more than firmware enumeration: the
OS could access the system volume after taking control. OpenBIOS EHCI and the
Mac OS X USB drivers are different implementations, so these results do not
establish general runtime USB stability.

The network screenshot shows an active 100baseTX full-duplex link on `en3`.
The packet counters shown were zero despite earlier successful networking.
That alone cannot distinguish missing driver statistics from a traffic stall.
Private network addresses, account names and console identifiers are omitted.

Incremental builds displayed the same November 12 build timestamp through
several fixes. For future tests, record the Git commit, ELF SHA-256 and builder
image digest as well as the startup diagnostic banner. Historical screenshots
cannot be mapped conclusively to individual source commits using that timestamp.

## Next tests

1. Record the exact build and repeat cold boots with the tested USB layout.
2. Check cancellation and fallback to the OpenBIOS prompt on the console.
3. Compare repeat reads of a known file and check runtime storage logs.
4. Test front/rear ports and additional keyboard/USB enclosure combinations.
5. Capture local network logs, gateway ping results and interface state while
   connectivity is failing; keep networking investigation separate from EHCI.

Host simulations in `tests/` cover selected production functions with mocked
hardware. They do not substitute for any of these hardware checks.
