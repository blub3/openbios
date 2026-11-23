# Xbox 360 GPU profile firmware

The driver-side boot profiles, archive recipes, and recorded USB checksums live
in [osx360-drivers on gpu/quartz-extreme](https://github.com/blub3/osx360-drivers/blob/gpu/quartz-extreme/docs/gpu-boot-profiles.md).

| Firmware source tag | Embedded archive | Profile family |
| --- | --- | --- |
| `gpu-fw-native-hdmi` | `NativeHD.mkext` | Native HDMI and the GPU profiles derived from that image, including `CMPFIX.ELF` and `WINTILE.ELF` |
| `gpu-fw-vblank-reserve` | `XenosVBR.mkext` | Double-buffer scanout reservation retry |
| `gpu-fw-vblank-queue` | `XenosVBQ.mkext` | Triple-buffer queued VBlank flip test |

Check out the appropriate firmware tag, then build target `xbox360-ppc`
using the repository's [Xbox 360 build instructions](../README.md). The
expected file is `obj-ppc/openbios-xbox360.elf`. For a profile derived
from the native HDMI image, use the utility from this branch:

```sh
python3 utils/xbox360-profile-elf.py native.elf NativeHD.mkext XWTC.MKE WINTILE.ELF
```

The archive basename is the only change in all 53 GPU images derived
from the native HDMI ELF at the November 23, 2026 checkpoint. The two
VBlank variants carry separate framebuffer reservation changes.
Use `--sha256` with the recorded manifest to check an exact reproduction.
Keep the original ELF and any existing boot configuration as rollbacks.
