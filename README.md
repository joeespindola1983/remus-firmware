# Remus Firmware

Greenfield firmware for the Remus Computer and Remus Blade, built with C++17,
ESP-IDF, and PlatformIO.

This repository currently implements **Milestone M0 only**: build foundations,
hardware-abstraction contracts, bounded core containers, minimal composition
roots, host tests, and CI. It intentionally contains no radio protocol, sensor
acquisition, GPS, storage writer, replay, handshake, or OTA implementation.

## Build

Requirements:

- PlatformIO Core 6.2.0
- PlatformIO Espressif32 platform 6.13.0
- ESP-IDF 5.5.3 (resolved by the pinned PlatformIO platform)
- CMake 3.16 or newer and a C++17 compiler for host tests

```sh
make test
make blade
make computer
make all
```

Native ESP-IDF builds are supported when an ESP-IDF 5.5.3 environment is
active:

```sh
idf.py -C firmware/blade build
idf.py -C firmware/computer build
```

The board target and GPIO values are provisional reference profiles until they
are verified against the assembled hardware. See
[`docs/HARDWARE_PROFILES.md`](docs/HARDWARE_PROFILES.md).

## Contracts

The normative implementation baseline is
[`docs/RFS-001-IMPLEMENTATION.md`](docs/RFS-001-IMPLEMENTATION.md). Architecture
boundaries are summarized in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

`deviceSerialNumber`, `deviceBootId`, and `recordingId` are numeric firmware
identities. Downstream adapters must serialize them losslessly (normally as
decimal strings) and preserve source-local clock semantics.

## Hardware evidence

Successful compilation is not a physical hardware gate. M0 does not claim USB
boot, heap, flash size, GPIO, encrypted-peer capacity, or on-device memory
measurements. Those remain pending bench evidence in
[`docs/BENCHMARKS.md`](docs/BENCHMARKS.md).
