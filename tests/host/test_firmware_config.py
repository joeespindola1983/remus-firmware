#!/usr/bin/env python3
import pathlib
import sys


def main() -> int:
    root = pathlib.Path(sys.argv[1])
    failures = []
    for role in ("computer", "blade"):
        defaults = root / "firmware" / role / "sdkconfig.defaults"
        values = defaults.read_text(encoding="utf-8").splitlines()
        if "CONFIG_FREERTOS_HZ=1000" not in values:
            failures.append(f"{defaults}: CONFIG_FREERTOS_HZ must be 1000")

    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
