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
