# H4 GPS and two-Blade field runbook

This gate validates the new Computer-only GPS path without weakening the
already field-tested Computer IMU, two-Blade association, replay, or RMS1
writer paths. Use the same `feature/m5-gps` commit on all three boards. The
Blade image intentionally contains no GPS component.

## 1. Preserve the rollback point

Record the tested commit and keep release `v1.0.0-alpha.2` available. Build all
three images before uploading anything:

```sh
git status --short --branch
pio run -d firmware/computer
pio run -d firmware/blade
```

Do not merge the feature branch into `develop` or `main` during this gate. If
GPS integration destabilizes capture, reinstall the published alpha.2 images
instead of changing wiring or provisioning during the same test.

## 2. Computer-only GPS baseline

With the microSD inserted, install the Computer image and leave both Blades
off. Connect the NEO-6M TX to Computer GPIO0 (RX), NEO-6M RX to GPIO1 (TX), and
use the already verified power/ground arrangement. Test outdoors with an open
sky view.

Expected serial state lines:

- `FIRMWARE: role=computer ... version=...` identifying the feature commit;
- `GPS: START` with UART pins, baud, buffer, and SD/RMS availability;
- one `GPS: CFG` result for every requested NMEA/UBX message and rate command;
- `GPS: READY requested_mode=normal target_rate_hz=1` once after startup;
- `GPS: MODE mode=normal status=APPLIED` only after receiver ACK and
  rate readback; a configuration fault falls back to continuous `normal` at 1 Hz;
- `GPS: NO_FIX` while acquisition is pending, then `GPS: FIX` after lock;
- `GPS: STALE` only if complete navigation epochs stop for more than 3 s;
- no periodic healthy GPS output; serial and `remus_pc.log` remain quiet after
  initialization unless fix/mode state changes or a problem counter increases;
- no `gps_decoder_overflows`, `gps_checksum_failures`, `gps_record_drops`, or
  `config_fault` problem event throughout the baseline;
- no coordinate stream in serial; full decoded solutions belong in
  `remus_pc.log` and RMS1.

If serial reports `SD diagnostic log unavailable` or `sdmmc_card_init failed`,
GPS diagnostics still continue on serial but no internal log or RMS1 file can
exist. Fix the card/power/wiring/mount problem before treating missing
`remus_pc.log` as a GPS failure.

Wait at least five minutes. Computer IMU batches and RMS checkpoints must keep
advancing before, during, and after first fix. A receiver with no fix is a valid
test condition and must still produce explicit unavailable GPS fields.

## 3. Add two Blades without restarting the Computer

Power Blade A, wait for `BOUND`, then power Blade B and wait for its independent
`BOUND`. Continue for at least 20 minutes, including a short interval with the
GPS antenna obstructed or moved indoors if this can be done safely.

Required behavior:

- the first Blade remains bound when the second joins;
- all three IMUs continue at their existing 200 Hz profile;
- GPS fix loss does not change radio state or pause RMS recording;
- `rms_dropped`, `rms_write_failures`, and GPS record drops remain zero;
- queue high-water marks remain bounded; GPS queue pressure never consumes the
  IMU/recovery queue; when `dynamicArmed` is exercised, queue depth 12 or a new
  GPS record drop must demote the receiver to `normal`;

Do not induce SD removal and RF loss in the same run. Execute the controlled M4
loss/replay runbook separately so a failure has one interpretable cause.

## 4. Inspect the card

After at least two seconds for the final periodic checkpoint, power down and
safely eject the card. Run the inspector on the new RMS1 file:

```sh
python3 tools/remus-inspect/remus_inspect.py /Volumes/REMUS/RM_*.rms
```

Acceptance evidence:

- one Computer and two distinct Blade source summaries;
- `gps_records` advances at approximately 1 per second of receiver output;
- `position_fixes` is nonzero during the open-sky interval;
- `ubx_coherent` is nonzero and normally tracks GPS records after startup;
- the final GPS position, satellite count, quality, HDOP, age, and flags are
  plausible; unavailable fields remain sentinels, not fabricated zeros;
- CRC, record ordinal, and payload checks pass; no silent IMU loss is accepted.

Archive the serial log, `remus_pc.log`, RMS1 inspector output, firmware commit,
duration, minimum heap, and all three queue high-water marks. A successful
build or a visible serial fix alone does not pass H4.
