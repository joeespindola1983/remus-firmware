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
        if "CONFIG_PARTITION_TABLE_CUSTOM=y" not in values:
            failures.append(f"{defaults}: custom 4 MiB partition table must be enabled")

        platformio = (root / "firmware" / role / "platformio.ini").read_text(
            encoding="utf-8"
        )
        if "board_build.partitions = partitions.csv" not in platformio:
            failures.append(f"firmware/{role}/platformio.ini: partitions.csv not selected")

        partitions = root / "firmware" / role / "partitions.csv"
        if not partitions.exists():
            failures.append(f"{partitions}: missing")
        else:
            text = partitions.read_text(encoding="utf-8")
            if "nvs,data,nvs,0x9000,0x6000" not in text.replace(" ", ""):
                failures.append(f"{partitions}: NVS layout would break provisioning")
            if "factory,app,factory,0x10000,0x300000" not in text.replace(" ", ""):
                failures.append(f"{partitions}: 3 MiB application partition missing")

    blade_cmake = (root / "firmware" / "blade" / "CMakeLists.txt").read_text(
        encoding="utf-8"
    ) + (root / "firmware" / "blade" / "src" / "CMakeLists.txt").read_text(
        encoding="utf-8"
    )
    for forbidden in ("remus_gps", "remus_platform_esp32_gps"):
        if forbidden in blade_cmake:
            failures.append(f"firmware/blade: must not depend on {forbidden}")

    computer_cmake = (root / "firmware" / "computer" / "CMakeLists.txt").read_text(
        encoding="utf-8"
    ) + (root / "firmware" / "computer" / "src" / "CMakeLists.txt").read_text(
        encoding="utf-8"
    )
    for required in ("remus_gps", "remus_platform_esp32_gps"):
        if required not in computer_cmake:
            failures.append(f"firmware/computer: missing {required}")

    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
