# Build and bench evidence

## M0 software baseline

The validation commands are:

```sh
make test
pio run -d firmware/blade -e blade
pio run -d firmware/computer -e computer
```

Validated on 2026-10-13 (macOS arm64):

| Item | Observed result |
|---|---|
| PlatformIO Core | 6.2.0 |
| Espressif32 platform | 6.13.0 |
| ESP-IDF | 5.5.3 (`framework-espidf` 3.50503.0) |
| RISC-V toolchain | 14.2.0+20251107 |
| `riscv32-esp-elf-g++` SHA-256 | `ba2d11db6e6d13cd80517f43d98133c3ea69828b866a91cc402e34e1a4c319eb` |
| Blade M2 clean build | PASS; RAM 57,620 B; app image 782,512 B |
| Computer M2 clean build | PASS; RAM 80,300 B; app image 890,400 B (radio + SD diagnostics) |
| Host CTest | PASS; 6/6 tests, including codec, registry, state, scheduler, supervisor, and log cases |
| Standalone `idf.py` build | INCONCLUSIVE; no standalone ESP-IDF shell was installed on this host |

The PlatformIO size report compares each app image with the default 1 MiB app
partition, not the complete provisional 4 MiB board flash. These are linked
image estimates, not measured free heap at runtime.

Both target CMake files explicitly register `src/` and shared components for a
native ESP-IDF build. That path still requires validation in an independently
installed ESP-IDF 5.5.3 shell; the PlatformIO-managed Python environment is not
a substitute for that gate.

## Physical evidence still required

### M2 partial bench capture — 2026-10-13

A user-supplied Computer serial capture demonstrated two separately provisioned
Blades joining after the Computer started. Both progressed from authenticated
candidate to application-confirmed `Bound`: the first at 6.857 s with RSSI
-65 dBm and the second at 14.867 s with RSSI -70 dBm. The registry reported two
sources; runtime queues, pools, and the SD diagnostic writer reported zero
drops or write failures. Stable identities and MAC addresses are intentionally
omitted from this evidence summary.

This proves the PC-first/two-Blade path. Blade-first boot order, individual
Blade restart/reassociation, and eight encrypted physical peers remain pending;
therefore M2 is not yet promoted to `develop`.

The Computer has been uploaded and its native USB Serial/JTAG monitor has
reconnected after reset. A sustained boot/heartbeat capture, minimum free heap,
physical flash capacity, GPIO safety, and idle/runtime RAM remain
`INCONCLUSIVE`. The diagnostic microSD mount, rotation at 512 KiB, recovery
after power loss, and card-removal behavior also require bench evidence. The
M2 H1/H2 association captures and physical eight-encrypted-peer capacity are
still required; host tests alone do not prove RF behavior. MPU-6050 FIFO/DRDY
jitter belongs to M3.

## M3 software baseline — 2026-10-13

The shared acquisition pipeline, MPU-6050 ESP-IDF driver, DATA_BATCH codec,
Blade transmitter, remote Computer RAM sink, and local Computer IMU RAM sink
are implemented on `feature/m3-imu-data`.

| Item | Observed result |
|---|---|
| Host CTest | PASS; 8/8 tests |
| DATA_BATCH maximum | PASS; 10 samples, 176-byte payload, 244-byte frame |
| Blade clean build | PASS; RAM 63,444 B; app image 814,738 B before final log-only rebuild |
| Blade final build | PASS; RAM 63,460 B; app image 815,080 B |
| Computer clean build | PASS; RAM 86,100 B; app image 917,644 B before final metrics rebuild |
| Computer final build | PASS; RAM 86,100 B; app image 918,088 B |

The driver supports FIFO burst reads, overflow detection/reset and an optional
DRDY ISR/task notification path. No DRDY GPIO is approved in the hardware
profile yet, so both role services deliberately log `mode=software_timer` and
measure actual read timestamps, missed periods and maximum observed jitter.
Gate H3 remains pending: it requires the two physical Blades, confirmed
role-specific GPIO/DRDY wiring, 20-minute captures and measured loss/jitter.

## M3/M5 partial bench capture — 2026-10-14

Firmware `f5eaae6` was exercised on one Computer and one Blade with the Computer
writing both raw IMU streams to the SD card. The resulting RMS1 file was parsed
with `tools/remus-inspect/remus_inspect.py`; stable device identities are omitted
from this evidence summary.

| Item | Observed result |
|---|---|
| Release host suite | PASS; 11/11 tests |
| Blade release build | PASS; RAM 63,476 B; app image 818,470 B |
| Computer release build | PASS; RAM 112,004 B; app image 933,308 B |
| Computer IMU | 15,520 samples over 77.595 s; 1,552 batches; zero inferred gaps |
| Blade IMU | 9,810 samples over 49.043 s; 981 batches; zero inferred gaps |
| Sample cadence | 5,000 us median for both sources; absolute P99 error 280 us (Computer) and 271 us (Blade) |
| Acquisition health | Zero IMU errors and zero missed samples at the final health report; maximum reported Computer jitter 1,449 us |
| RMS1 writer | 2,535 valid records; zero drops and zero write failures; queue high-water mark 3 |
| Diagnostic writer | Zero drops and zero write failures; queue high-water mark 1 |
| Radio | No reported DATA gap, send timeout, radio restart, or quarantine; observed RSSI from -47 to -59 dBm |

This capture validates the repaired acquisition scheduler, separated SD queues,
and transmit-callback recovery for the observed single-Blade interval. It does
not close H3: a sustained capture with two Blades, Blade restart/reassociation,
and the remaining boot-order cases are still required. GPS, replay/ACK
checkpointing, clean session finalization, and OTA remain outside this captured
firmware slice.

## M4 software baseline — 2026-10-14

Selective replay and distinct RAM/durable ACK progress are implemented on
`feature/m4-ack-replay`. This is software evidence, not the physical RF-loss
acceptance gate.

| Item | Observed result |
|---|---|
| Host CTest | PASS; 12/12 tests |
| Deterministic loss simulation | PASS; dropped sequence requested, replayed, deduplicated, and checkpoint-released |
| ACK invariants | PASS; writer backpressure cannot advance RAM ACK and no checkpoint is invented before sync |
| GAP invariants | PASS; long contiguous gaps advance safely; invalid out-of-window gaps are rejected atomically |
| Blade build | PASS; RAM 119,396 B (36.4%); app image 824,140 B |
| Computer build | PASS; RAM 112,900 B (34.5%); app image 938,828 B |
| Application partition | 3 MiB; Blade 26.2%, Computer 29.8% |
| Provisioning compatibility | NVS preserved at offset `0x9000`, size `0x6000` |

The Computer ACK cadence is ten accepted batches, immediate on a gap, or a
500 ms heartbeat. The Blade retains 256 batches and does not release them on a
RAM-only ACK. The durable watermark originates only after RMS1 `fflush` and
`fsync`; an irreversible loss is represented by a persisted RMS1 `GAP` record.
The inspector validates and reports explicit gap records separately from gaps
inferred from batch/sample discontinuity.

Physical acceptance remains pending: install this firmware on the Computer and
both Blades, then execute
[`tests/hardware/runbooks/M4_ACK_REPLAY.md`](../tests/hardware/runbooks/M4_ACK_REPLAY.md).
Required evidence includes controlled packet loss, replay recovery without
duplicate RMS batches, bounded replay depth, and continued 200 Hz acquisition.

## Two-Blade field capture for v1.0.0-alpha.2 — 2026-10-14

The integration revision `98d3b39` was installed on one Computer and two
separately provisioned Blades. The Computer boot log reported
`v1.0.0-alpha.1-8-g98d3b39-dirty`; the dirty suffix came from unrelated local
RMS1 work that is not included in this release. The resulting RMS1 artifact was
validated with `tools/remus-inspect/remus_inspect.py`. Stable physical device
identities and route information are intentionally omitted here.

| Source | Recorded samples | Approximate coverage | Effective IMU profile |
|---|---:|---:|---|
| Computer IMU | 452,680 | 37 min 43 s | +/-8 g, +/-1000 dps, 200 Hz |
| Blade A | 431,140 | 35 min 56 s | +/-16 g, +/-2000 dps, 200 Hz |
| Blade B | 423,984 | 35 min 20 s | +/-16 g, +/-2000 dps, 200 Hz |

The artifact contained all three source manifests and no explicit irreversible
gap record. The current inspector's `inferred_gaps` counter increments on
arrival-order transitions caused by selective replay, so it is not evidence of
final missing batches; final sequence-set reconciliation remains an inspector
follow-up. This field capture demonstrates concurrent useful acquisition from
the Computer and two Blades, but does not replace the controlled M4 loss tests.

Release qualification for `v1.0.0-alpha.2` therefore covers the observed
three-source field path, role-specific verified IMU profiles, bounded serial
diagnostics, selective replay, durable RMS1 checkpointing, and Blade
reassociation after a new boot identity at host-test level. GPS, controlled
Blade restart evidence, induced recoverable/unrecoverable RF loss, clean
session finalization, and eight physical encrypted peers remain pending.
