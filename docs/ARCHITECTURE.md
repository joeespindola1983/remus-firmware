# M0 architecture

The Remus firmware produces two independent images from one repository:

```text
remus_core <- remus_hal <- firmware/blade
     ^            ^      <- firmware/computer
     |
remus_diagnostics
remus_link -> remus_hal::ICrypto
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

## Current boundary

There is no protocol, radio, cryptography implementation, acquisition, session
storage, navigation, replay, session control, or time synchronization in M0.
`ICrypto` is a dependency-injection contract; `FakeCrypto` exists only for
deterministic host tests and is not secure or linked into firmware.
