# ADR-023: Bounded Computer microSD diagnostic log

- Status: accepted
- Date: 2026-10-13

## Context

The Remus Computer must remain diagnosable away from a serial monitor. A debug
log must not consume the card without bound, stall time-sensitive producers, or
be mistaken for canonical recording evidence.

## Decision

Only the Computer mounts the reference SPI microSD interface. Producers submit
bounded text events to a fixed 32-entry queue without waiting. A low-priority
task performs all filesystem work and synchronizes every accepted line.

The active file is `remus_pc.log`. Before a line would take it beyond 512 KiB,
the previous `remus_pc.log.1` is removed and the active file is renamed to that
archive. A new active file is then created. This two-segment policy retains
approximately 1 MiB of the newest diagnostics without repeatedly rewriting a
large FAT file to remove its first lines.

Mount/open failure is non-fatal. Queue drops, write failures, and rotations are
reported by the serial health heartbeat. Diagnostic records must not contain
secrets, provisioning material, or routinely expose stable device identities.

## Consequences

Recent operational history survives reset and no dynamic allocation is needed
for queued records. A sudden power loss can lose the line currently being
written, but prior lines are explicitly flushed and synchronized. This log is
an engineering channel only: future RMS1/session storage retains its own
schema, lifecycle, integrity, and raw-to-derived lineage.
