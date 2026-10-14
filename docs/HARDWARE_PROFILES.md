# Hardware profiles (M0 reference only)

No hardware profile is physically qualified by M0. The build target is
`esp32-c3-devkitm-1`, representing an ESP32-C3 with a provisional 4 MiB flash
size supplied by the board definition and `sdkconfig.defaults`. Custom
partitions are intentionally deferred until the exact modules and flash are
inspected; this software setting is not evidence of physical capacity.

## Blade v1 reference

- MCU: ESP32-C3 Super Mini/DevKitM-1; exact revision and flash unverified
- IMU: MPU-6050/GY-521; SDA GPIO 6, SCL GPIO 5, preserved from the
  owner-confirmed `remus-sensor` Blade profile
- DRDY GPIO is unassigned; production firmware therefore reports software
  timer mode until the interrupt wire is approved and physically validated
- No SD, GPS, or display
- USB: boot/console capability pending assembled-device verification

The current P0 software candidate requests MPU-6050 accelerometer range ±16 g,
gyroscope range ±2000 degrees/s, DLPF code 3 and 200 Hz. The driver writes and
reads back those register fields before exposing a verified effective profile.
This choice increases headroom for rapid equipment motion but reduces
sensitivity per count; it is not physically qualified until stationary-noise,
known-motion and near-rail measurements are captured on the assembled Blade.

## Computer v1 reference

- MCU: ESP32-C3; exact board and flash unverified
- Local MPU-6050 has an independent Computer role profile using SDA GPIO 6 and
  SCL GPIO 5, preserved from the owner-confirmed `remus-sensor` Computer
  profile. The equal pin numbers do not couple the two role profiles or their
  physical I2C buses.
- DRDY GPIO is unassigned and must be confirmed separately for the Computer.
- Future NEO-6M: ESP RX GPIO 0, TX GPIO 1
- Diagnostic microSD adapter: MISO 8, MOSI 10, SCK 20, CS 21; 10 MHz SPI,
  software integration complete but electrical/card compatibility unverified
- Display pins and display support are outside the MVP

The current P0 software candidate requests MPU-6050 accelerometer range ±8 g,
gyroscope range ±1000 degrees/s, DLPF code 3 and 200 Hz for the Computer. Its
`SourceManifest` is generated from the register profile that the driver read
back, not from duplicated constants. This remains a bench candidate rather
than evidence that the physical module has the nominal MPU-6050 sensitivity.

These values are evidence from the existing prototype profile, not approval of
an electrical design. A bench review must verify boot straps, USB/UART conflicts,
power rails, module markings, wiring, and flash before a profile is frozen.
