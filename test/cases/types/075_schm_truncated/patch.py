#!/usr/bin/env python3
"""Truncate .fb inside SCHM (v11+ has JBMX after SCHM; do not strip file tail)."""
import struct
import sys

path = sys.argv[1]
data = open(path, "rb").read()
if len(data) < 36:
    sys.exit(1)
schm_off = struct.unpack_from("<I", data, 28)[0]
if schm_off == 0 or schm_off >= len(data):
    sys.exit(1)
trunc = schm_off + 8
if trunc >= len(data):
    sys.exit(1)
open(path, "wb").write(data[:trunc])
