# ADR-024: Extended bounded Computer diagnostic log

- Status: accepted
- Date: 2026-10-15
- Supersedes: ADR-023 segment-size decision

## Context

The original pair of 512 KiB segments retained only about 1 MiB. GPS solution
events and health snapshots can consume that during a normal water session,
removing the startup and acquisition evidence needed to diagnose a failure.
The microSD card has substantially more capacity, while a larger segment does
not require a larger RAM queue or change the canonical RMS1 recording.

## Decision

Keep the two-file rotation policy and fixed diagnostic queue, but increase each
segment to 16 MiB. `remus_pc.log` and `remus_pc.log.1` therefore retain at most
32 MiB in total. The segment limit lives in a host-tested profile shared by the
Computer adapter and tests.

Diagnostic writes remain lower priority than RMS1 writes. They use the existing
periodic durability checkpoint, and queue overflow or I/O failure remains
explicitly counted. The log remains an engineering channel: it does not replace
RMS1 evidence and must not contain credentials or sensitive stable identities.
It is event-driven: initialization, connection state, fix transitions and new
fault deltas are retained, while routine GPS solutions and periodic healthy
snapshots remain exclusively in RMS1/counters.

## Consequences

Many hours of recent operational history can survive a long session or several
short sessions. Rotation remains a bounded rename/create operation rather than
an unbounded append. Maximum card use grows from 1 MiB to 32 MiB without
increasing hot-path allocation, queue capacity, or firmware RAM consumption.
