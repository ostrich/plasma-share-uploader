#!/usr/bin/env python3
import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: copy_with_prefix.py INPUT OUTPUT PREFIX", file=sys.stderr)
        return 2

    in_path = pathlib.Path(sys.argv[1])
    out_path = pathlib.Path(sys.argv[2])
    prefix = sys.argv[3]
    out_path.write_text(prefix + in_path.read_text(encoding="utf-8"), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
