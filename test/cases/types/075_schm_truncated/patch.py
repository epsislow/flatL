#!/usr/bin/env python3
import sys

path = sys.argv[1]
data = open(path, "rb").read()[:-3]
open(path, "wb").write(data)
