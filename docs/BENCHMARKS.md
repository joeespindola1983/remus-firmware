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
| Blade build | PASS; RAM 9,532 B; app image 160,096 B |
| Computer build | PASS; RAM 9,532 B; app image 160,136 B |
| Host CTest | PASS; 3/3 tests |
| Standalone `idf.py` build | INCONCLUSIVE; no standalone ESP-IDF shell was installed on this host |

The PlatformIO size report compares each app image with the default 1 MiB app
partition, not the complete provisional 4 MiB board flash. These are linked
image estimates, not measured free heap at runtime.

Both target CMake files explicitly register `src/` and shared components for a
native ESP-IDF build. That path still requires validation in an independently
installed ESP-IDF 5.5.3 shell; the PlatformIO-managed Python environment is not
a substitute for that gate.

## Physical evidence still required

M0 has not executed a device upload or boot. USB console output, minimum free
heap, physical flash capacity, GPIO safety, and idle/runtime RAM are therefore
`INCONCLUSIVE`, not passing. The eight-encrypted-peer capacity and MPU-6050
FIFO/DRDY jitter are M-1 bench gates and are not implemented here.
