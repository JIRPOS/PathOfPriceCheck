#!/usr/bin/env python3
"""Emit one C byte array for a binary file, on stdout: `bin2c.py <symbol> <file>`.

The bytes go in **as they are** — not compressed, not encoded. That costs a few kilobytes of
executable over the base85-and-stb_compress idiom this replaced, and buys a `.rdata` a scanner can
recognise: a TTF still starts with a table directory and a PNG with its signature, where an encoded
blob paired with a routine that decodes it into a fresh buffer is shaped exactly like a packer, and
gets scored that way on an unsigned binary. See docs/architecture.md.
"""

import sys

if len(sys.argv) != 3:
    sys.exit("usage: bin2c.py <symbol> <file>")

symbol, path = sys.argv[1], sys.argv[2]
with open(path, "rb") as f:
    data = f.read()
if not data:
    sys.exit(f"{path} is empty")

out = sys.stdout
out.write(f"static const unsigned char {symbol}[{len(data)}] = {{\n")
for i in range(0, len(data), 16):
    out.write("    " + "".join(f"0x{b:02x}," for b in data[i : i + 16]) + "\n")
out.write("};\n")
