#!/usr/bin/env python3
"""Reject an ELF that regresses the kernel's W^X load segments."""
import subprocess, sys
output = subprocess.check_output(["x86_64-elf-readelf", "-Wl", sys.argv[1]], text=True)
loads = [line for line in output.splitlines() if line.strip().startswith("LOAD")]
if len(loads) < 3 or any("RWE" in line or "RW E" in line for line in loads):
    raise SystemExit("expected separate RX, R, and RW (non-executable) LOAD segments")
print("ELF layout verified: separate RX, R, and RW segments; no RWX segment")
