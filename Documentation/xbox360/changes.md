# Fork change record

## Experimental integration — 2026-11-14

These changes extend upstream baseline
`d6f664856e184e4f939606cc364d69ada678c71e`. The dates describe this integration
record; they do not replace the original authorship or copyright notices.

- `drivers/usbehci.c`: add polling EHCI control/bulk storage support with
  explicit endian conversion, aligned DMA buffers, bounded polling and
  per-port fallback to OHCI for unsupported classes.
- `drivers/usb.c`, `drivers/usb.h`, `include/drivers/usb.h`, `drivers/pci.c`,
  `drivers/pci_database.c`, `drivers/pci_database.h`: integrate EHCI discovery,
  deferred controller startup and correct controller parent selection.
- `drivers/usbohci.c`, `drivers/usbohci_rh.c`, `drivers/usbhid.c`: improve
  startup connection scanning, keyboard registration and handoff diagnostics.
- `drivers/usbmsc.c`: use distinct disk unit addresses/aliases; print model,
  capacity, sector size and speed; reject unsupported capacity results.
- `drivers/build.xml`, `config/examples/ppc_xbox360_config.xml`: include and
  enable the experimental EHCI driver for the Xbox target.
- `arch/ppc/xbox360/init.c`, `arch/ppc/xbox360/start.S`: controller diagnostics,
  boot-delay binding and exception-path fixes used during bring-up.
- `arch/ppc/xbox360/macosx/macosx.c`: extend mkext lookup across USB aliases.
- `arch/ppc/xbox360/xbox360.fs`: add the tested SSD `mac-boot` path and
  cancellable automatic startup.
- `tests/`: host simulations for EHCI transfers, class handoff and keyboard
  startup, using selected production functions and mocked hardware services.
- `README`, `README.md`, `.gitignore`, `Documentation/xbox360/`: fork scope,
  attribution, build/test/boot instructions, test evidence and publishing notes.

Known limitations and unresolved installation/network behavior are listed in
[hardware-results.md](hardware-results.md) and [usb2-bringup.md](usb2-bringup.md).
No networking fix or successful direct DVD installation is claimed.
