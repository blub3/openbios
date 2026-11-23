#!/usr/bin/env python3
"""Select a boot archive in a tagged Xbox 360 OpenBIOS ELF without relinking it."""

import argparse
import hashlib
import re
from pathlib import Path


def profile_image(image: bytes, original: str, archive: str) -> bytes:
    if image[:7] != b"\x7fELF\x01\x02\x01" or image[16:20] != b"\x00\x02\x00\x14":
        raise ValueError("expected a 32-bit big-endian PowerPC executable")
    for name in (original, archive):
        if not re.fullmatch(r"[A-Za-z0-9_.-]+\.(?:mkext|MKE)", name):
            raise ValueError(f"invalid mkext basename {name!r}")
    before = original.encode("ascii") + b"\0"
    after = archive.encode("ascii") + b"\0"
    if len(after) > len(before):
        raise ValueError("the archive name exceeds the source image's slot")
    if image.count(before) != 1:
        raise ValueError("expected exactly one occurrence of the original archive name")
    if before != after and after in image:
        raise ValueError("the selected archive name already appears in the image")
    return image.replace(before, after.ljust(len(before), b"\0"), 1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("original")
    parser.add_argument("archive")
    parser.add_argument("output", type=Path)
    parser.add_argument("--sha256", help="expected SHA-256 of the new image")
    args = parser.parse_args()
    try:
        updated = profile_image(args.source.read_bytes(), args.original, args.archive)
        digest = hashlib.sha256(updated).hexdigest()
        if args.sha256 is not None and args.sha256.lower() != digest:
            raise ValueError(f"SHA-256 mismatch: got {digest}")
        with args.output.open("xb") as output:
            output.write(updated)
        print(f"{digest}  {args.output}")
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
