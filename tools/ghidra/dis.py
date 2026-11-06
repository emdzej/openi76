#!/usr/bin/env python3
"""Formats ghidra-cli's JSON disassembly as one instruction per line (stdin -> stdout); used by dis.sh."""
import json
import sys

data = json.load(sys.stdin)
if isinstance(data, dict):
    data = data.get("data", data.get("instructions", []))
for e in data:
    if isinstance(e, dict) and "address" in e:
        print(f"{e['address']}  {e['mnemonic'].lower():7} {', '.join(e['operands'])}")
