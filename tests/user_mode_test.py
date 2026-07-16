#!/usr/bin/env python3
"""Verify first user process/thread objects and audited syscalls."""
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
        if needle in text or needle in text.replace("\r", "").replace("\n", ""):
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


def send_text(process, text):
    for character in text:
        send_monitor(process, "sendkey " + {"\n": "ret"}.get(character, character))


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
    logical_lines = []
    current = ""
    for raw_line in text.splitlines():
        start = raw_line.find("event seq=")
        if start >= 0:
            if current:
                logical_lines.append(current)
            current = raw_line[start:]
        elif current:
            stripped = raw_line.strip()
            if raw_line[:1].isspace():
                current += " " + stripped
            else:
                current += stripped

        if current and EVENT_RE.search(current):
            logical_lines.append(current)
            current = ""
    if current:
        logical_lines.append(current)
    return [match.groupdict() for match in EVENT_RE.finditer("\n".join(logical_lines))]


def matching(events, **fields):
    return [
        event for event in events
        if all(event.get(key) == value for key, value in fields.items())
    ]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def require_syscall(events, op, result="ok"):
    syscalls = matching(events, kind="syscall", actor="thread/1", op=op, result=result)
    require(syscalls, f"missing syscall {op} {result}")
    tx = syscalls[-1]["tx"]
    require(matching(events, tx=tx, kind="transaction", actor="thread/1", op="begin", result="ok"),
            f"missing transaction begin for syscall {op}")
    if result == "ok":
        require(matching(events, tx=tx, kind="transaction", actor="thread/1", op="commit", result="ok"),
                f"missing transaction commit for syscall {op}")
    return syscalls[-1]


def main():
    elf = Path(sys.argv[1]).resolve()
    qemu = os.environ.get("QEMU", "qemu-system-x86_64")
    build = ROOT / "cmake-build-debug"
    log = build / "user-mode-serial.log"
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
            send_text(process, "userdemo\n")
            toggle_monitor(process)
            text = wait_for(log, "User demo complete: cpl3=yes syscall_return=yes fault_contained=yes", process)
            time.sleep(0.2)
            text = log.read_text(errors="replace")

            events = parse_events(text)
            require(matching(events, kind="state", actor="scheduler", target="process/1", op="process_create", result="ok"),
                    "missing process creation event")
            require(matching(events, kind="state", actor="scheduler", target="thread/1", op="thread_create", result="ok"),
                    "missing thread creation event")
            require(matching(events, kind="state", actor="scheduler", target="thread/1", op="user_transition_scaffold", result="ok"),
                    "missing user transition scaffold event")
            require(matching(events, kind="action", actor="memory", target="page_tables", op="map_user", result="ok",
                             policy="allow:user-code-page"),
                    "missing user code page mapping event")
            require(matching(events, kind="action", actor="memory", target="page_tables", op="map_user", result="ok",
                             policy="allow:user-stack-page"),
                    "missing user stack page mapping event")
            require(matching(events, kind="state", actor="scheduler", target="thread/1", op="enter_cpl3", result="ok",
                             policy="allow:user-enter"),
                    "missing CPL3 entry event")

            traps = matching(events, kind="syscall", actor="thread/1", target="kernel",
                             op="cpl3_trap", result="ok", policy="allow:cpl3")
            require(len(traps) >= 4, "missing CPL3 syscall trap events")

            inspect = require_syscall(events, "inspect")
            require("allow:syscall:inspect" in inspect["policy"], "inspect syscall did not carry policy allow")
            yielded = require_syscall(events, "yield")
            require("allow:syscall:yield" in yielded["policy"], "yield syscall did not carry policy allow")
            config_get = require_syscall(events, "config_get")
            require(config_get["target"] == "/config/theme", "config_get syscall used wrong target")
            require("allow:syscall:config_get" in config_get["policy"], "config_get syscall did not carry policy allow")

            denied_policy = matching(events, kind="policy", actor="thread/1", target="/config/theme",
                                     op="policy_check", result="denied", policy="deny:policy-default,rev=1,rule=0")
            require(denied_policy, "missing denied syscall policy event")
            denied_tx = denied_policy[-1]["tx"]
            require(matching(events, tx=denied_tx, kind="transaction", actor="thread/1", op="abort", result="denied"),
                    "missing denied syscall abort")
            faults = matching(events, kind="fault", actor="thread/1", target="kernel",
                              op="page_fault", result="fault", policy="deny:user-page-fault")
            require(faults,
                    "missing contained user page fault event")
            require("User page fault contained:" in text, "missing page fault containment log")
            fault_seq = int(faults[-1]["seq"])
            shell_commits = [
                event for event in matching(events, kind="transaction", actor="shell",
                                            op="commit", result="ok")
                if int(event["seq"]) > fault_seq
            ]
            require(shell_commits, "missing shell transaction commit after contained page fault")
            print("User mode test passed. Serial log:", log)
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
        print("user mode test failed:", error, file=sys.stderr)
        sys.exit(1)
