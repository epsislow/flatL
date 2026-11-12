#!/usr/bin/env python3
import struct
import sys

path = sys.argv[1]
data = bytearray(open(path, "rb").read())
schm_off = struct.unpack_from("<I", data, 28)[0]
idx = data.find(bytes([0x78, 0x10]), schm_off)
if idx < 0:
    sys.exit(2)
data[idx + 2] = 99
data[idx + 3] = 0
open(path, "wb").write(data)
