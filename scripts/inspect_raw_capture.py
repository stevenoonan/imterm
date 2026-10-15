#!/usr/bin/env python3
"""Inspect serial bytes without sending captured escape sequences to the terminal."""

import argparse
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", help="Raw RX .bin file")
    parser.add_argument("--offset", type=lambda value: int(value, 0), default=0,
                        help="Starting byte offset, decimal or 0x-prefixed (default: 0)")
    parser.add_argument("--length", type=lambda value: int(value, 0), default=4096,
                        help="Number of bytes to inspect (default: 4096)")
    parser.add_argument("--sgr", action="store_true",
                        help="List SGR color/format sequences in the selected range")
    args = parser.parse_args()
    if args.offset < 0 or args.length < 0:
        parser.error("offset and length must be nonnegative")
    try:
        with open(args.file, "rb") as stream:
            stream.seek(args.offset)
            data = stream.read(args.length)
    except OSError as error:
        parser.error(str(error))
    if args.sgr:
        matches = list(re.finditer(rb"\x1b\[([0-9;:]*)m", data))
        for match in matches:
            parameters = match.group(1).decode("ascii") or "0"
            print(f"{args.offset + match.start():08x}  {match.group()!r}  SGR {parameters}")
        if not matches:
            print("No SGR sequences in the selected byte range.")
    else:
        for offset in range(0, len(data), 16):
            chunk = data[offset:offset + 16]
            print(f"{args.offset + offset:08x}  {chunk.hex(' '):47s}  {chunk!r}")


if __name__ == "__main__":
    main()
