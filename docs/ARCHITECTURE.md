# M0 architecture

The Remus firmware produces two independent images from one repository:

```text
remus_core <- remus_hal <- firmware/blade
     ^            ^      <- firmware/computer
     |
remus_diagnostics
```

M0 establishes contracts only. `remus_core`, `remus_hal`, and
`remus_diagnostics` are host-buildable and contain no ESP-IDF or FreeRTOS
headers. The two `Runtime` classes are deliberately small composition roots and
do not yet create hardware drivers or work loops.

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

## Current boundary

There is no protocol, radio, cryptography implementation, acquisition, storage,
navigation, replay, session control, or time synchronization in M0. `ICrypto`
is a dependency-injection contract; `FakeCrypto` exists only for deterministic
host tests and is not secure or linked into firmware.
