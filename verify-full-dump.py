#!/usr/bin/env python3
"""Verify repeated full dumps and agreement with the marked partial image."""

import argparse
import hashlib
from pathlib import Path


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument("pass1", type=Path)
parser.add_argument("pass2", type=Path)
parser.add_argument("--partial", required=True, type=Path)
args = parser.parse_args()

first = args.pass1.read_bytes()
second = args.pass2.read_bytes()
partial = args.partial.read_bytes()

print(f"{args.pass1}: {len(first)} bytes, SHA-256 {digest(first)}")
print(f"{args.pass2}: {len(second)} bytes, SHA-256 {digest(second)}")

if first != second:
    mismatch = next(i for i, pair in enumerate(zip(first, second)) if pair[0] != pair[1])
    raise SystemExit(f"FAIL: full passes differ first at offset 0x{mismatch:X}")
print("PASS: full dumps are byte-for-byte identical")

if len(first) < len(partial):
    raise SystemExit("FAIL: full dump is shorter than the marked partial image")
if len(partial) % 4:
    raise SystemExit("FAIL: partial image length is not word-aligned")

marker = bytes.fromhex("efbeadde")
checked = 0
for offset in range(0, len(partial), 4):
    word = partial[offset : offset + 4]
    if word == marker:
        continue
    checked += 1
    if first[offset : offset + 4] != word:
        raise SystemExit(f"FAIL: recovered partial word differs at offset 0x{offset:X}")

print(f"PASS: all {checked} recovered partial words agree with the full dump")
