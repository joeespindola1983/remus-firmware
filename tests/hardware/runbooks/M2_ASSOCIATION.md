# M2 ESP-NOW association gate

This gate requires one Remus Computer and at least one Remus Blade. It does not
exercise IMU capture, recording, or data frames.

## Provisioning

Choose a unique non-zero `deviceSerialNumber` for every board, one non-zero
`networkId`, and one random 32-byte network secret shared only by this test
network. Never commit or paste the secret into a log.

```sh
./tools/provision_nvs.sh /dev/cu.usbmodem-PC PC_DEVICE_ID NETWORK_ID SECRET_HEX
./tools/provision_nvs.sh /dev/cu.usbmodem-BLADE BLADE_DEVICE_ID NETWORK_ID SECRET_HEX
```

Firmware upload does not erase the NVS partition, so provisioning is normally a
one-time maintenance action. Reprovisioning deliberately replaces the three
Remus credential values in NVS.

## H1 — one Blade, either boot order

1. Upload the `computer` environment to the Computer and `blade` to the Blade.
2. Start both serial monitors at 115200 baud.
3. First boot the Blade, then the Computer. Capture the complete logs.
4. Repeat with the Computer booted first.
5. Confirm the Blade reports `Discovering`, `HelloReady`, then `Bound`.
6. Confirm the Computer reports `blade candidate` and then
   `blade state=Bound`. A send callback alone is not sufficient.
7. If an SD card is present, confirm `radio_discovering`, `blade_candidate`, and
   `blade_bound` exist in `/sdcard/remus_pc.log` (or its `.1` rotation).

## H2 — two Blades and capacity behavior

1. Provision the second Blade with a different `deviceSerialNumber` and the same
   network credentials.
2. Boot the boards in varying orders and confirm both reach `Bound` independently.
3. Reset either Blade and confirm it associates using its new boot ID without
   changing the other Blade's association.
4. Automated host coverage proves the eight-entry registry bound and rejection
   of a ninth entry. Claiming eight encrypted physical peers still requires an
   eight-Blade bench capture.

Store captured logs under `tests/hardware/evidence/` only after removing serial
numbers, MAC addresses, secrets, routes, and personal data.
