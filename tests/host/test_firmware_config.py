#!/usr/bin/env python3
import pathlib
import re
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

    gps_header = (root / "firmware" / "computer" / "src" / "GpsService.hpp").read_text(
        encoding="utf-8"
    )
    stack_match = re.search(r"kTaskStackBytes\s*=\s*(\d+)", gps_header)
    if stack_match is None or int(stack_match.group(1)) < 8192:
        failures.append("GpsService: task stack must remain at least 8192 bytes")

    computer_source = (root / "firmware" / "computer" / "src" / "main.cpp").read_text(
        encoding="utf-8"
    )
    gps_source = (root / "firmware" / "computer" / "src" / "GpsService.cpp").read_text(
        encoding="utf-8"
    )
    for forbidden in ('"health"', '"radio_recovery"'):
        if forbidden in computer_source:
            failures.append(f"Computer text log must not emit routine {forbidden} snapshots")
    if "GPS: HEALTH" in gps_source or '"gps_health"' in gps_source:
        failures.append("GPS text output must be event/problem driven, not periodic health")

    espnow_source = (
        root / "components" / "remus_platform_esp32" / "src" / "EspNowDriver.cpp"
    ).read_text(encoding="utf-8")
    if "Status EspNowDriver::restartLinkLayer()" not in espnow_source:
        failures.append("EspNowDriver: recovery must preserve the running Wi-Fi driver")
    for role in ("computer", "blade"):
        radio_source = (root / "firmware" / role / "src" / (
            "ComputerRadioService.cpp" if role == "computer" else "BladeRadioService.cpp"
        )).read_text(encoding="utf-8")
        recovery = radio_source.split("recoverRadio", 1)[-1]
        if "radio_.restartLinkLayer()" not in recovery:
            failures.append(f"{role}: radio recovery must restart only the ESP-NOW link layer")

    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
