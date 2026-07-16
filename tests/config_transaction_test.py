#!/usr/bin/env python3
"""Verify transaction-backed config set/get and audit history."""
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
    "\n": "ret",
}


def send_text(process, text):
    for character in text:
        send_monitor(process, "sendkey " + KEYS.get(character, character))


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
    log = build / "config-serial.log"
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
            send_text(process, "set /config/theme dark\n")
            toggle_monitor(process)
            text = wait_for(log, "/config/theme=dark revision 1->2", process)
            set_events = parse_events(text)
            command = matching(set_events, kind="command", actor="shell", target="/config/theme", op="command", policy="set")
            require(command, "missing set command event")
            command_seq = command[-1]["seq"]
            begin = [
                event for event in set_events
                if event["kind"] == "transaction" and
                event["actor"] == "shell" and
                event["target"].startswith("transaction/") and
                event["op"] == "begin" and
                event["parent"] == command_seq
            ]
            require(begin, "missing set transaction begin")
            tx = begin[-1]["seq"]
            toggle_monitor(process)
            send_text(process, "get /config/theme\n")
            toggle_monitor(process)
            text = wait_for(log, "/config/theme=dark", process)
            toggle_monitor(process)
            send_text(process, "history /config/theme\n")
            toggle_monitor(process)
            text = wait_for(log, "op=history", process)
            toggle_monitor(process)
            send_text(process, f"why tx {tx}\n")
            toggle_monitor(process)
            text = wait_for(log, f"transaction/{tx} actor=shell target=/config/theme status=committed", process)

            events = parse_events(text)
            policy = matching(events, tx=tx, kind="policy", actor="shell", target="/config/theme", op="policy_check", result="ok")
            state_update = matching(events, tx=tx, kind="state", actor="object", target="/config/theme", op="state_update", result="ok")
            set_event = matching(events, tx=tx, kind="state", actor="config", target="/config/theme", op="set", result="ok")
            commit = matching(events, tx=tx, kind="transaction", actor="shell", target=f"transaction/{tx}", op="commit", result="ok")
            require(policy, "missing config:set policy event")
            require(state_update, "missing config state revision event")
            require(set_event, "missing config set state event")
            require(commit, "missing config transaction commit")
            require(int(begin[-1]["seq"]) < int(policy[-1]["seq"]), "policy did not follow begin")
            require(int(policy[-1]["seq"]) < int(state_update[-1]["seq"]), "state update did not follow policy")
            require(int(state_update[-1]["target_rev"]) == 2, "state update did not report revised object revision")
            require(int(set_event[-1]["target_rev"]) == 2, "set event did not report revised object revision")
            require(int(set_event[-1]["seq"]) < int(commit[-1]["seq"]), "commit did not follow set event")
            require("op=get result=ok" in text, "get did not emit a successful config event")
            require("state_update" in text and "target_rev=2" in text, "history did not include config mutation")
            require(f"transaction/{tx} actor=shell target=/config/theme status=committed" in text, "why tx did not explain committed set transaction")
            print("Config transaction test passed. Serial log:", log)
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
        print("config transaction test failed:", error, file=sys.stderr)
        sys.exit(1)
