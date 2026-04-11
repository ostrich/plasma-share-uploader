#!/usr/bin/env python3
import sys
import time


def main() -> int:
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
    time.sleep(seconds)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
