#!/usr/bin/env python3
import struct
import sys

path = sys.argv[1]
data = bytearray(open(path, "rb").read())
schm_off = struct.unpack_from("<I", data, 28)[0]
idx = data.find(b"q", schm_off)
if idx < 0:
    sys.exit(2)
data[idx + 1] = 255
open(path, "wb").write(data)
