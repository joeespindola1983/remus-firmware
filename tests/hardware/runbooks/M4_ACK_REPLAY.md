# M4 ACK and selective-replay bench runbook

This gate validates one Computer and two Blades with the same M4 firmware
revision. It intentionally excludes GPS. Use a FAT-formatted microSD card in
the Computer and provision all three boards for the same network with distinct
device serial numbers.

## 1. Install and establish the baseline

Build and upload `computer` to the Computer and `blade` to each Blade. Start the
Computer monitor, power both Blades, and wait at least two minutes without
inducing loss. Record the firmware revision and the three health snapshots.

Expected baseline:

- both Blades reach `Bound` and continuously produce IMU data;
- `replay_depth` remains bounded by `replay_capacity=256`;
- `ack_windows` and `checkpoint_acks_queued` increase;
- `replay_unavailable`, `unrecoverable_gaps`, RMS write failures, and queue
  drops remain zero;
- the Computer local IMU and both Blade sources continue sampling.

## 2. Controlled recoverable loss

Do not disconnect an antenna from a powered radio and do not short or disturb
power wiring. Temporarily attenuate one Blade only—for example, place it in a
metal container with the lid partly closed—for about two seconds, then restore
normal placement. Keep the other Blade visible as a control. Repeat once for
the other Blade.

Expected recovery:

- the affected source increments `replay_requests_queued` on the Computer and
  `replay_queued` on its Blade;
- `duplicate_batches` may increase if an original and replay both arrive;
- the unaffected Blade and Computer IMU continue without interruption;
- after SD checkpoint ACKs arrive, replay depth falls again;
- no `replay_unavailable` or `unrecoverable_gaps` is expected for this short
  attenuation interval.

## 3. Bounded-loss behavior

Only after step 2 passes, attenuate one Blade long enough to exceed its 256
batch ring (approximately 12.8 seconds at 20 full batches/s), then restore it.
This is a data-loss test, not a recovery-success test.

Expected behavior:

- `replay_overflows` and `gap_notices` increase on that Blade;
- the Computer logs `unrecoverable_data_gap` for an explicit finite range;
- after the RMS1 `GAP` record is written and synchronized, checkpoint progress
  resumes rather than looping forever on the missing range;
- acquisition remains live and memory/queue counters remain bounded.

## 4. Evidence inspection

Stop power only after allowing at least two seconds for the last checkpoint,
safely eject the card, and run:

```sh
python3 tools/remus-inspect/remus_inspect.py /Volumes/REMUS/RM_*.rms
```

For the recoverable-loss file, each source must show no explicit gaps and no
duplicate logical batches. For the bounded-loss file, the affected source must
show `explicit_gaps` and `lost_batches` matching the serial/SD diagnostics.
Record final free/minimum heap, queue high-water marks, replay counters, RMS
write failures, and the inspector summary. A build alone does not pass M4.
