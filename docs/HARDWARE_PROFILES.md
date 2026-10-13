# Hardware profiles (M0 reference only)

No hardware profile is physically qualified by M0. The build target is
`esp32-c3-devkitm-1`, representing an ESP32-C3 with a provisional 4 MiB flash
size supplied by the board definition and `sdkconfig.defaults`. Custom
partitions are intentionally deferred until the exact modules and flash are
inspected; this software setting is not evidence of physical capacity.

## Blade v1 reference

- MCU: ESP32-C3 Super Mini/DevKitM-1; exact revision and flash unverified
- Future IMU: MPU-6050/GY-521; SDA GPIO 6, SCL GPIO 5 reference only
- No SD, GPS, or display
- USB: boot/console capability pending assembled-device verification

## Computer v1 reference

- MCU: ESP32-C3; exact board and flash unverified
- Future local MPU-6050: SDA GPIO 6, SCL GPIO 5
- Future NEO-6M: ESP RX GPIO 0, TX GPIO 1
- Diagnostic microSD adapter: MISO 8, MOSI 10, SCK 20, CS 21; 10 MHz SPI,
  software integration complete but electrical/card compatibility unverified
- Display pins and display support are outside the MVP

These values are evidence from the existing prototype profile, not approval of
an electrical design. A bench review must verify boot straps, USB/UART conflicts,
power rails, module markings, wiring, and flash before a profile is frozen.
