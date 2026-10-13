# ADR-018: Computer autonomy and Blade subordination

- Status: accepted for MVP
- Date: 2026-10-13

## Decision

The Remus Computer must be able to record with zero through eight Blades. A
Remus Blade never creates an autonomous recording and cannot stream without a
valid Computer session.

M0 enforces only the dependency boundary: Computer and Blade are separate
images, and Blade has no GPS or storage interface dependency. Runtime behavior
arrives in later gated milestones.
