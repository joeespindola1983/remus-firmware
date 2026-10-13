# RemusRF v1 connection protocol

RemusRF v1 uses a 48-byte header, typed payload, CRC32C and a 16-byte truncated
HMAC-SHA256 tag. Frames are little-endian and never exceed the ESP-NOW v1
application limit of 250 bytes. The implementation writes every field
explicitly and never serializes native C++ structure layout.

## Connection messages

| Type | Code | Payload | Authentication |
|---|---:|---:|---|
| `BEACON` | `0x01` | 28 bytes | `groupAuthKey` |
| `HELLO` | `0x02` | 44 bytes | `groupAuthKey` |
| `BIND` | `0x03` | 32 bytes | `pairAuthKey` plus ESP-NOW LMK |
| `BIND_ACK` | `0x04` | 32 bytes | `pairAuthKey` plus ESP-NOW LMK |
| `SOURCE_MANIFEST` | `0x11` | 32 bytes | `pairAuthKey` plus ESP-NOW LMK |

The exact field order and offsets are normative in
[`RFS-001-IMPLEMENTATION.md`](RFS-001-IMPLEMENTATION.md), sections 9.2 and 9.5.
Reserved bytes must be zero when encoded and are rejected when nonzero.

## Validation order

The decoder validates bounds, magic/header invariants, wire version, declared
lengths, CRC32C and HMAC before exposing a decoded payload. State, network,
sender, boot, recording and epoch authorization remains the responsibility of
the M2 state machines. The caller selects the permitted discovery or pair key;
the frame cannot choose its own trust context.

## Key derivation

All keys use HKDF-SHA256 over the 32-byte provisioned `networkSecret`:

- discovery authentication: info `remus/rf-v1/discovery`, salt `networkId` LE;
- PMK: info `remus/rf-v1/espnow-pmk`, salt `networkId` LE;
- pair authentication: info `remus/rf-v1/pair-auth`;
- ESP-NOW LMK: info `remus/rf-v1/espnow-lmk`.

Pair salts contain the two canonical `deviceSerialNumber` values in ascending
order, each encoded as `u64` LE. Reversing local/remote roles therefore derives
the same pair keys. Key material must never be logged.
