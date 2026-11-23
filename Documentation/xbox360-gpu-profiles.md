# Xbox 360 GPU profile firmware

The driver-side boot profiles, archive recipes, and recorded USB checksums live
in [osx360-drivers on gpu/quartz-extreme](https://github.com/blub3/osx360-drivers/blob/gpu/quartz-extreme/docs/gpu-boot-profiles.md).

| Firmware source tag | Embedded archive | Profile family |
| --- | --- | --- |
| `gpu-fw-native-hdmi` | `NativeHD.mkext` | Native HDMI and the GPU profiles derived from that image, including `CMPFIX.ELF`, `WINTILE.ELF`, `WINAUTO.ELF` and `WINOVR.ELF` |
| `gpu-fw-vblank-reserve` | `XenosVBR.mkext` | Double-buffer scanout reservation retry |
| `gpu-fw-vblank-queue` | `XenosVBQ.mkext` | Triple-buffer queued VBlank flip test |

Check out the appropriate firmware tag, then build target `xbox360-ppc`
using the repository's [Xbox 360 build instructions](../README.md). The
expected file is `obj-ppc/openbios-xbox360.elf`. For a profile derived
from the native HDMI image, use the utility from this branch:

```sh
python3 utils/xbox360-profile-elf.py native.elf NativeHD.mkext XWAU.MKE WINAUTO.ELF
python3 utils/xbox360-profile-elf.py native.elf NativeHD.mkext XWOV.MKE WINOVR.ELF
```

Both commands reproduce the cold-boot-tested WindowServer ELFs
byte-for-byte from the archived native HDMI ELF. The only change in each
image is its archive basename. The two VBlank variants carry separate
framebuffer reservation changes. Use `--sha256` with the recorded manifest
to check an exact reproduction. The prerelease provides both ELFs, the
base image, and corresponding source and build notes. Keep the original
ELF and boot configuration as rollbacks. [Firmware prerelease](https://github.com/blub3/openbios/releases/tag/gpu-fw-windowserver) · [Companion driver prerelease](https://github.com/blub3/osx360-drivers/releases/tag/gpu-0.0.5d48-gpuwinoverlap)
