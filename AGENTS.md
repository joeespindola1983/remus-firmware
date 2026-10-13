# Remus firmware contribution rules

These rules apply to all code, tests, fixtures, schemas, documentation, prompts,
and generated artifacts in this repository.

## Scope and source of truth

- Read `docs/RFS-001-IMPLEMENTATION.md` before changing architecture or milestone
  scope.
- Implement one milestone per coherent branch/PR. Do not anticipate a later
  milestone.
- This is a greenfield repository. Do not copy legacy `remus-sensor` application
  code or preserve its architecture as compatibility behavior.
- Architectural decisions marked immutable require an ADR and owner approval
  before implementation changes.

## Domain language is a contract

- Use the canonical identifiers `deviceSerialNumber`, `deviceBootId`,
  `recordingId`, `crewSeatNumber`, and `boatSide`.
- Use English identifiers in code and durable artifacts. Translated labels belong
  at product boundaries.
- Unknown, unavailable, and zero are distinct. Never fabricate samples, time,
  source identity, placement, or continuity.
- Keep physical device identity separate from MAC address, seat, athlete, side,
  transport device, and recording identity.
- Keep source-native timestamps and clock domains distinct. Receipt time is not
  acquisition time.
- Before introducing or renaming a domain concept, update the canonical Remus
  data dictionary and machine-readable terminology catalog first.

## Required TDD flow

1. Add or change a deterministic host test that expresses the behavior.
2. Run it and record the expected failure (RED).
3. Implement the smallest cohesive change that passes it (GREEN).
4. Refactor without changing behavior and rerun all host tests.
5. Build both firmware targets unless the milestone explicitly permits a
   host-only change.

Tests must not use wall-clock sleeps for state behavior. Use fakes and explicit
time advancement. Never claim a physical gate without its captured bench log.

## Architecture boundaries

- `remus_core` and domain components must compile on a host without ESP-IDF or
  FreeRTOS headers.
- Hardware access is only through `remus_hal` interfaces. Drivers are adapters;
  they do not own business state transitions.
- Blade code must not include or link GPS or storage capabilities.
- No allocation is permitted in a sample, packet, callback, or other hot path.
  Use bounded queues and pools with explicit overflow behavior.
- Radio callbacks must copy bounded bytes and enqueue a descriptor only. They do
  not parse, write storage, retry synchronously, or block.
- RAM acceptance and durable storage checkpoint are separate states.
- Every queue or pool has one declared owner, finite capacity, and measurable
  overflow behavior.

## Quality and security

- Never commit credentials, keys, real device identities, raw NVS dumps, athlete
  personal data, routes, or secrets.
- Never log key material or sensitive stable identities in routine diagnostics.
- Do not hardcode physical IDs or GPIOs in domain code.
- Public methods document inputs, errors, context, side effects, ownership, and
  bounds.
- Run `git diff --check`, formatting, host tests, and both builds before review.
- Wire/file changes require explicit versioning and golden fixtures in the
  milestone that owns those formats.

## Git workflow

- `main` is the releasable branch; `develop` is the integration branch.
- Work on short-lived `feature/<milestone>-<topic>` or
  `fix/<milestone>-<topic>` branches from `develop`.
- Keep commits cohesive and reviewable. Do not mix milestone work, generated
  binaries, formatting churn, or unrelated refactors.
- Merge reviewed features into `develop`; promote qualified milestones to
  `main`. Tag only evidence-backed releases.
