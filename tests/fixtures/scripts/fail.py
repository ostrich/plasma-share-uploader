#!/usr/bin/env python3
import sys


def main() -> int:
    message = sys.argv[2] if len(sys.argv) > 2 else "intentional failure"
    print(message, file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
