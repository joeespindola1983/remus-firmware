# ADR-001: one repository, two firmware images

- Status: accepted for MVP
- Date: 2026-10-13

## Decision

Keep shared bounded C++ components under `components/` and maintain independent
ESP-IDF projects under `firmware/blade` and `firmware/computer`. Each image has
its own configuration, composition root, build output, and hardware gate.

## Consequences

Shared code must remain host-buildable. The Blade image cannot acquire storage
or GPS dependencies through the Computer. Both images are built on every CI
change.
