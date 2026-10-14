# Remus Firmware

Greenfield firmware for the Remus Computer and Remus Blade, built with C++17,
ESP-IDF, and PlatformIO.

This repository currently implements the M0-M3 acquisition path and an early,
GPS-free slice of the M5 RMS1 writer: build foundations, bounded diagnostics,
authenticated ESP-NOW association, 200 Hz raw IMU acquisition on Computer and
Blade, DATA_BATCH transport, and append-only recording on the Computer SD card.
Replay/ACK checkpointing, GPS, clean session finalization, and OTA are not yet
implemented.

M2 broadcasts authenticated `BEACON`/`HELLO`, derives per-pair keys, registers
encrypted peers, and considers a Blade bound only after its authenticated
`BIND_ACK`. Physical H1/H2 evidence is still required before M2 is accepted.

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

The ESP32-C3 native USB Serial/JTAG interface is the primary console for both
targets. When `sdkconfig.defaults` changes, remove the generated
`firmware/<target>/sdkconfig.<environment>` file before rebuilding so ESP-IDF
regenerates the effective configuration.

When a FAT-formatted microSD card is present in the Computer, operational events
are also written to `remus_pc.log`. At 512 KiB it rotates to `remus_pc.log.1`;
the next rotation replaces the archive, bounding both files to 1 MiB total.
Card absence or write failure does not stop the Computer. This engineering log
is deliberately separate from session evidence. While a session is active, raw
local and remote IMU batches are stored in a uniquely named
`RM_<recordingId>.rms` file. The writer checkpoints with `fsync` at least once
per second or each 32 KiB and stops accepting evidence after an SD write or
checkpoint failure.

After safely ejecting the card, validate an RMS1 file and optionally export all
raw IMU samples to CSV:

```sh
python3 tools/remus-inspect/remus_inspect.py /Volumes/REMUS/RM_*.rms
python3 tools/remus-inspect/remus_inspect.py /Volumes/REMUS/RM_*.rms --csv imu.csv
```

The inspector verifies session and record CRC32C values, record ordinals, and
DATA_BATCH sizes. It reports every source independently and infers discontinuity
counts from batch and sample indices. Each source also persists a
`SOURCE_MANIFEST`; CSV output retains the raw axes and adds calibrated g/dps
columns derived from that recorded metadata.

Before the first M2 bench test, provision each board's NVS using
[`tools/provision_nvs.sh`](tools/provision_nvs.sh) and follow
[`tests/hardware/runbooks/M2_ASSOCIATION.md`](tests/hardware/runbooks/M2_ASSOCIATION.md).

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

Successful compilation is not a physical hardware gate. Encrypted-peer
capacity, association under both boot orders, and on-device memory measurements
remain pending bench evidence in
[`docs/BENCHMARKS.md`](docs/BENCHMARKS.md).
