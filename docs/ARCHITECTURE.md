# Firmware architecture through M4

The Remus firmware produces two independent images from one repository:

```text
remus_core <- remus_hal <- firmware/blade
     ^            ^      <- firmware/computer
     |
remus_diagnostics
remus_link -> remus_hal::{ICrypto,IRadio}
```

M0 establishes the product contracts. `remus_core`, `remus_hal`, and
`remus_diagnostics` are host-buildable and contain no ESP-IDF or FreeRTOS
headers. The Computer composition root additionally owns an ESP-IDF microSD
diagnostic adapter; the Blade has no storage dependency.

## Identity and evidence

- `deviceSerialNumber` identifies a provisioned physical device.
- `deviceBootId` identifies one power/reset execution cycle.
- `recordingId` identifies one continuous recording segment.
- MAC address, crew seat, boat side, athlete, and transport are never identities
  for the physical source.
- Firmware uses fixed-width numeric identity types. Cross-platform adapters must
  preserve their full precision and expose canonical field names.

## Dependency rules

- Core code depends only on the C++17 standard library.
- HAL interfaces may depend on core types, never on ESP-IDF.
- Target application code may depend on core, HAL, diagnostics, and future
  platform adapters.
- Blade targets never gain GPS or block-storage dependencies.
- The ESP32 platform adapter and all behavior beyond empty boot roots belong to
  later milestones.

## Bounded memory

`FixedQueue`, `StaticEventQueue`, and `PacketPool` own fixed storage. Full queues
reject writes without overwriting live data. `PacketPool` uses generation-tagged
handles so stale or duplicate releases fail explicitly.

## Computer diagnostic log

The Computer's microSD log is an engineering observability channel, not a
recording/session store and not RMS1 evidence. Producers enqueue fixed-size
records without blocking. A low-priority writer task owns FAT file I/O, flushes
and synchronizes each accepted line, and rotates two 512 KiB text segments.
Queue overflow and I/O failure are counted and remain visible in the serial
health heartbeat. Missing or faulty storage degrades diagnostics only; it does
not prevent the Computer runtime from starting.

Log records contain monotonic time, severity, event name, and bounded details.
Secrets, provisioning keys, stable identities, and raw evidence do not belong
in this channel.

## RemusRF v1 codec boundary

`remus_link` owns the host-testable binary envelope, connection-message codecs,
CRC32C validation and key derivation policy. Cryptographic operations remain
behind `ICrypto`; the ESP32 adapter uses mbedTLS. The codec has no ESP-NOW,
FreeRTOS, state-machine or storage dependency. M1 deliberately does not start
the radio or claim a physical Blade connection.

## M2 radio boundary

ESP-NOW callbacks only copy bounded events into a fixed queue. Target-owned radio
services parse and authenticate frames, while portable state machines own link
transitions. `TxScheduler` permits one SDK send in flight, `SourceRegistry`
bounds the Computer to eight sources, and `RadioSupervisor` turns isolated and
persistent failures into explicit policy actions. MAC delivery never marks a
Blade bound; only a valid pair-authenticated `BIND_ACK` can do that.

## M3 acquisition and M4 recovery boundary

Both roles sample the MPU-6050 at 200 Hz through the shared acquisition
pipeline and produce bounded ten-sample `DATA_BATCH` payloads. Role-specific
composition roots own their GPIO profiles; the driver and batching semantics
remain shared.

The Blade radio owner retains encoded batches in a fixed 256-entry
`ReplayWindow`. The Computer's per-source `AckTracker` separates frame
validation, writer admission, and durable SD checkpoint progress. Out-of-order
admission produces a 32-bit selective window and bounded `REPLAY_REQUEST`;
overflow produces an explicit `GAP_NOTICE`. Replay traffic is limited to four
batches per request so new acquisition traffic continues to make progress.

The SD task is the only owner of RMS1 file I/O. It publishes a per-source
checkpoint watermark only after successful `fflush` and `fsync`. An
irrecoverable range is first appended as an RMS1 `GAP` record; therefore a
radio ACK cannot silently convert missing evidence into durable data.

There is still no GPS, explicit START/STOP session lifecycle, clean session
finalization, recovery-on-boot, OTA, or time synchronization. `FakeCrypto`
remains host-test-only.
