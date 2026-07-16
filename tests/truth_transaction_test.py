#!/usr/bin/env python3
"""Drive the shell through QEMU and verify truth-layer transaction events."""
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


KEYS = {
    " ": "spc",
    "/": "slash",
    ".": "dot",
    "\n": "ret",
}


def send_text(process, text):
    for character in text:
        key = KEYS.get(character)
        if key is None and "A" <= character <= "Z":
            key = "shift-" + character.lower()
        elif key is None:
            key = character
        send_monitor(process, "sendkey " + key)


def toggle_monitor(process):
    if process.stdin is None:
        raise RuntimeError("QEMU stdin is not available")
    process.stdin.write(b"\x01c")
    process.stdin.flush()
    time.sleep(0.1)


EVENT_RE = re.compile(
    r"event seq=(?P<seq>\d+) tx=(?P<tx>\d+) kind=(?P<kind>\S+) "
    r"actor#=(?P<actor_id>\d+) target#=(?P<target_id>\d+) "
    r"target_rev=(?P<target_rev>\d+) "
    r"parent=(?P<parent>\d+) "
    r"actor=(?P<actor>\S+) target=(?P<target>\S+) op=(?P<op>\S+) "
    r"result=(?P<result>\S+) policy=(?P<policy>\S+)"
)


def parse_events(text):
    return [match.groupdict() for match in EVENT_RE.finditer(text)]


def matching(events, **fields):
    return [
        event for event in events
        if all(event.get(key) == value for key, value in fields.items())
    ]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    elf = Path(sys.argv[1]).resolve()
    qemu = os.environ.get("QEMU", "qemu-system-x86_64")
    build = ROOT / "cmake-build-debug"
    log = build / "truth-serial.log"
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
            send_text(process, "cat /README.TXT\n")
            toggle_monitor(process)
            text = wait_for(log, "Use ls to list files", process)
            toggle_monitor(process)
            send_text(process, "expand /README.TXT\n")
            toggle_monitor(process)
            text = wait_for(log, "available mutations:", process)
            toggle_monitor(process)
            send_text(process, "history /README.TXT\n")
            toggle_monitor(process)
            text = wait_for(log, "op=history", process)

            events = parse_events(text)
            command = matching(events, kind="command", actor="shell", target="/README.TXT", op="command", policy="cat")
            require(command, "missing cat command event")
            command_seq = command[-1]["seq"]

            begin = [
                event for event in events
                if event["kind"] == "transaction" and
                event["actor"] == "shell" and
                event["op"] == "begin" and
                event["parent"] == command_seq
            ]
            require(begin, "missing transaction begin after cat command")
            tx = begin[-1]["seq"]
            require(begin[-1]["tx"] == tx, "transaction begin did not use its sequence as tx id")

            policy = matching(events, tx=tx, kind="policy", actor="shell", target="/README.TXT", op="policy_check", result="ok")
            lookup = matching(events, tx=tx, kind="state", actor="ramfs", target="/README.TXT", op="lookup", result="ok")
            read = matching(events, tx=tx, kind="state", actor="ramfs", target="/README.TXT", op="read", result="ok")
            commit = matching(events, tx=tx, kind="transaction", actor="shell", target=f"transaction/{tx}", op="commit", result="ok")
            require(policy, "missing policy event for cat transaction")
            require(lookup, "missing lookup event for cat transaction")
            require(read, "missing read event for cat transaction")
            require(commit, "missing transaction commit for cat transaction")
            require(int(command_seq) < int(begin[-1]["seq"]), "transaction begin did not follow command event")
            require(int(begin[-1]["seq"]) < int(policy[-1]["seq"]), "policy check did not follow transaction begin")
            require(int(policy[-1]["seq"]) < int(read[-1]["seq"]), "concrete read did not follow policy check")
            require(int(read[-1]["seq"]) < int(commit[-1]["seq"]), "transaction commit did not follow action events")
            require("rev=1" in policy[-1]["policy"], "policy decision does not report revision")
            require("rule=" in policy[-1]["policy"], "policy decision does not report rule")
            require("rev=1" in read[-1]["policy"], "read action does not carry policy revision")
            require("rule=" in read[-1]["policy"], "read action does not carry policy rule")
            require(policy[-1]["target_id"] == read[-1]["target_id"], "policy/read target ids differ")
            require(int(read[-1]["target_rev"]) >= 1, "read action did not report target revision")
            require("available next expansions:" in text, "expand did not list next expansions")
            require("cat /README.TXT requires policy filesystem:read" in text, "expand did not list guarded file mutation")
            require("relevant capabilities:" in text, "expand did not list relevant capabilities")
            require("capability/shell/filesystem:read/*" in text, "expand did not show filesystem read capability")
            require(f"target=transaction/{tx} op=commit" in text, "history did not render transaction commit")
            require("history /README.TXT" in text and "kind=query" in text, "history command did not render query events")
            print("Truth transaction test passed. Serial log:", log)
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
        print("truth transaction test failed:", error, file=sys.stderr)
        sys.exit(1)
