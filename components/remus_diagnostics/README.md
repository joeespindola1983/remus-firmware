# remus_diagnostics

Minimal M0 counters and bounded event records. Callers own event-log storage and
drain policy. Counter increments are non-blocking relaxed atomics. Per-packet or
per-sample verbose logging is intentionally absent.
