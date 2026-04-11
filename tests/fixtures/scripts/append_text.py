#!/usr/bin/env python3
import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: append_text.py FILE TEXT", file=sys.stderr)
        return 2

    path = pathlib.Path(sys.argv[1])
    text = sys.argv[2]
    path.write_text(path.read_text(encoding="utf-8") + text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
