#!/usr/bin/env python3
"""Generate a test file of random 64-bit integers, one per line.

Uses os.urandom + struct for fast random int generation (no numpy needed).

Usage:
    python3 generate_data.py --output data/input.txt --size 1GB
"""

import argparse
import os
import struct
import sys
import time


def generate(output_path: str, target_bytes: int) -> int:
    """Write random int64 values (one per line) until file reaches target size."""
    batch = 500_000  # ints per write batch
    fmt = f"<{batch}q"  # little-endian signed 64-bit
    total_count = 0

    start = time.time()
    with open(output_path, "w") as f:
        while True:
            raw = os.urandom(batch * 8)
            ints = struct.unpack(fmt, raw)
            text = "\n".join(map(str, ints))
            f.write(text)
            f.write("\n")
            total_count += batch

            written = f.tell()
            if total_count % 2_000_000 == 0:
                elapsed = time.time() - start
                rate = written / elapsed / 1e6 if elapsed > 0 else 0
                print(
                    f"\r  {written / 1e9:.2f} GB | "
                    f"{total_count:>12,} ints | "
                    f"{rate:.0f} MB/s",
                    end="",
                    file=sys.stderr,
                    flush=True,
                )

            if written >= target_bytes:
                break

    elapsed = time.time() - start
    size = os.path.getsize(output_path)
    print(
        f"\nDone: {size / 2**30:.3f} GiB, {total_count:,} integers, {elapsed:.1f}s",
        file=sys.stderr,
    )
    return total_count


def parse_size(s: str) -> int:
    """Parse human-readable size like '1GB' into bytes."""
    s = s.strip().upper()
    for suffix, mult in [("GIB", 2**30), ("GB", 10**9),
                         ("MIB", 2**20), ("MB", 10**6),
                         ("KIB", 2**10), ("KB", 10**3)]:
        if s.endswith(suffix):
            return int(float(s[: -len(suffix)]) * mult)
    return int(s)


def main():
    parser = argparse.ArgumentParser(description="Generate random integer test data")
    parser.add_argument("--output", "-o", default="data/input.txt",
                        help="Output file path (default: data/input.txt)")
    parser.add_argument("--size", "-s", default="1GB",
                        help="Target file size, e.g. 100MB, 1GB (default: 1GB)")
    args = parser.parse_args()

    out_dir = os.path.dirname(os.path.abspath(args.output))
    os.makedirs(out_dir, exist_ok=True)

    target = parse_size(args.size)
    print(f"Generating ~{target / 2**30:.2f} GiB -> {args.output}", file=sys.stderr)
    generate(args.output, target)


if __name__ == "__main__":
    main()
