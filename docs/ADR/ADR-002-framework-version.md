# ADR-002: pinned build framework

- Status: accepted for M0
- Date: 2026-10-13

## Decision

Pin PlatformIO Core `6.2.0` in CI and PlatformIO Espressif32 `6.13.0` in both
projects. That platform resolves ESP-IDF `5.5.3` and the associated Espressif
RISC-V toolchain. Use C++17 in target and host builds.

Native `idf.py` builds use ESP-IDF `5.5.3`; developers must activate that exact
release before invoking the native commands.

## Rationale

The RFS requires an ESP-IDF 5.x baseline and a pinned PlatformIO platform.
Espressif32 7.x moves to ESP-IDF 6.x and is therefore outside the approved M0
baseline. Version 6.13.0 is the latest 6.x release supporting ESP-IDF 5.5.3.

## Verification

CI and local build logs record resolved package versions. `docs/BENCHMARKS.md`
records the exact toolchain package/version observed during M0 validation.
