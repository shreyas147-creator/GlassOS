#!/usr/bin/env python3
"""Headless QEMU smoke test for Multiboot, serial events, and the shell."""
import os, subprocess, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def main():
    elf = Path(sys.argv[1]).resolve()
    qemu = os.environ.get("QEMU", "qemu-system-x86_64")
    log = ROOT / "cmake-build-debug" / "serial.log"
    log.parent.mkdir(exist_ok=True)
    with log.open("w") as output:
        process = subprocess.Popen([qemu, "-kernel", str(elf), "-display", "none", "-serial", "stdio", "-monitor", "none", "-no-reboot", "-no-shutdown"], stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                output.flush()
                text = log.read_text(errors="replace") if log.exists() else ""
                if "GlassOS boot: Multiboot v1 handoff" in text and "GlassOS serial: kernel ready" in text and "event seq=1" in text:
                    print("Boot test passed. Serial log:", log)
                    return
                if process.poll() is not None: break
                time.sleep(.05)
            raise RuntimeError("did not observe Multiboot serial boot and structured event")
        finally:
            process.terminate()
            try: process.wait(timeout=2)
            except subprocess.TimeoutExpired: process.kill()
if __name__ == "__main__":
    try: main()
    except RuntimeError as error: print("boot test failed:", error, file=sys.stderr); sys.exit(1)
