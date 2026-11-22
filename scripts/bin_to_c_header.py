# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Embed a binary file (e.g. a compiled .metallib) into a C header byte array."""
from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--symbol", required=True)
    args = parser.parse_args()

    data = args.input.read_bytes()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "/* Generated file; do not edit. */",
        "#include <stddef.h>",
        f"static const unsigned char {args.symbol}[] = {{",
    ]
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append(f"static const size_t {args.symbol}_size = {len(data)}u;")
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
