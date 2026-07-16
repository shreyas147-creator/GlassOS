#!/usr/bin/env python3
"""Drive a denied policy check and verify revision/rule/default metadata."""
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def wait_for(log, needle, process, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = log.read_text(errors="replace") if log.exists() else ""
        if needle in text:
            return text
        if process.poll() is not None:
            break
        time.sleep(0.05)
    raise RuntimeError(f"did not observe {needle!r}")


def send_monitor(process, command):
    if process.stdin is None:
        raise RuntimeError("QEMU stdin is not available")
    process.stdin.write((command + "\n").encode())
    process.stdin.flush()
    time.sleep(0.025)


def toggle_monitor(process):
    if process.stdin is None:
        raise RuntimeError("QEMU stdin is not available")
    process.stdin.write(b"\x01c")
    process.stdin.flush()
    time.sleep(0.1)


def send_text(process, text):
    keys = {" ": "spc", "-": "minus", ":": "shift-semicolon", "\n": "ret"}
    for character in text:
        send_monitor(process, "sendkey " + keys.get(character, character))


EVENT_RE = re.compile(
    r"event seq=(?P<seq>\d+) tx=(?P<tx>\d+) kind=(?P<kind>\S+) "
    r"actor#=(?P<actor_id>\d+) target#=(?P<target_id>\d+) "
    r"target_rev=(?P<target_rev>\d+) "
    r"parent=(?P<parent>\d+) "
    r"actor=(?P<actor>\S+) target=(?P<target>\S+) op=(?P<op>\S+) "
    r"result=(?P<result>\S+) policy=(?P<policy>\S+)"
)


def main():
    elf = Path(sys.argv[1]).resolve()
    qemu = os.environ.get("QEMU", "qemu-system-x86_64")
    build = ROOT / "cmake-build-debug"
    log = build / "policy-deny-serial.log"
    log.parent.mkdir(exist_ok=True)

    with log.open("w") as output:
        process = subprocess.Popen([
            qemu,
            "-kernel", str(elf),
            "-display", "none",
            "-serial", "mon:stdio",
            "-monitor", "none",
            "-no-reboot",
            "-no-shutdown",
        ], stdin=subprocess.PIPE, stdout=output, stderr=subprocess.STDOUT)
        try:
            wait_for(log, "GlassOS serial: kernel ready", process)
            toggle_monitor(process)
            send_text(process, "deny-policy\n")
            toggle_monitor(process)
            text = wait_for(log, "deny:policy-default,rev=1,rule=0", process)
            denied = [
                match.groupdict()
                for match in EVENT_RE.finditer(text)
                if match.group("kind") == "policy" and
                   match.group("actor") == "shell" and
                   match.group("target") == "kernel" and
                   match.group("op") == "policy_check" and
                   match.group("result") == "denied"
            ]
            if not denied:
                raise RuntimeError("missing denied policy event")
            if denied[-1]["policy"] != "deny:policy-default,rev=1,rule=0":
                raise RuntimeError("denied policy metadata was not default rev/rule")
            aborted = [
                match.groupdict()
                for match in EVENT_RE.finditer(text)
                if match.group("kind") == "transaction" and
                   match.group("actor") == "shell" and
                   match.group("op") == "abort" and
                   match.group("result") == "denied"
            ]
            if not aborted:
                raise RuntimeError("missing transaction abort for denied policy")
            toggle_monitor(process)
            send_text(process, "policy allow shell kernel:reboot kernel\n")
            toggle_monitor(process)
            text = wait_for(log, "policy revision=2", process)
            toggle_monitor(process)
            send_text(process, "policy show\n")
            toggle_monitor(process)
            text = wait_for(log, "capability/shell/kernel:reboot/kernel", process)
            toggle_monitor(process)
            send_text(process, "deny-policy\n")
            toggle_monitor(process)
            text = wait_for(log, "policy=allow:kernel:reboot,rev=2", process)
            allowed = [
                match.groupdict()
                for match in EVENT_RE.finditer(text)
                if match.group("kind") == "state" and
                   match.group("actor") == "shell" and
                   match.group("target") == "kernel" and
                   match.group("op") == "reboot" and
                   match.group("result") == "ok" and
                   match.group("policy").startswith("allow:kernel:reboot,rev=2,rule=")
            ]
            if not allowed:
                raise RuntimeError("runtime allow rule did not permit retry")
            print("Policy deny test passed. Serial log:", log)
        finally:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as error:
        print("policy deny test failed:", error, file=sys.stderr)
        sys.exit(1)
