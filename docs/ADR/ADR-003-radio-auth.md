# ADR-003: Authenticated RemusRF discovery and encrypted unicast

- Status: accepted by RFS-001
- Date: 2026-10-13

Broadcast `BEACON` and `HELLO` frames use an HMAC key derived from the
provisioned group secret because ESP-NOW does not encrypt broadcast traffic.
After authenticated discovery, both devices derive identical pair keys from
their canonically ordered physical device identifiers. `BIND`, `BIND_ACK` and
later unicast traffic require both an application HMAC and ESP-NOW LMK.

The transport MAC is metadata, not physical identity or an authentication
secret. The decoder never accepts a key-selection instruction from an
unauthenticated frame. The development group PSK profile is suitable for a
controlled prototype only and does not claim resistance to extraction from a
compromised group device.
