# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify the bundled reference ZCode font (Z52 Hara Tig) in third_party/fonts/
against its SHA-256, re-downloading it from its pinned source commit when it
is missing or different. See NOTICE.md for its provenance and license status."""
import hashlib
import pathlib
import sys
import urllib.request

URL = ("https://raw.githubusercontent.com/zmongol/mongol_editor_package-main/"
       "618edf0237c6ec18d38b4fe07ab244f2f98a3d7f/assets/fonts/z52haratig.ttf")
SHA256 = "2bc510505c6f52935506a3d342758ed44b271164c7b4841415ff7a94172db2ef"


def main() -> int:
    dest = pathlib.Path(__file__).resolve().parent.parent / "third_party" / "fonts" / "z52haratig.ttf"
    if dest.exists() and hashlib.sha256(dest.read_bytes()).hexdigest() == SHA256:
        print(f"{dest} already present")
        return 0
    with urllib.request.urlopen(URL, timeout=60) as response:
        data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        print(f"SHA-256 mismatch: got {digest}, want {SHA256}", file=sys.stderr)
        return 1
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
    print(f"wrote {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
