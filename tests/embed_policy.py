#!/usr/bin/env python3
"""Generate a C string include from the versioned policy file."""
import sys
from pathlib import Path


def quote_line(line: str) -> str:
    escaped = line.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}\\n"\n'


def main() -> None:
    source = Path(sys.argv[1])
    target = Path(sys.argv[2])
    text = source.read_text()
    target.write_text("".join(quote_line(line) for line in text.splitlines()))


if __name__ == "__main__":
    main()
