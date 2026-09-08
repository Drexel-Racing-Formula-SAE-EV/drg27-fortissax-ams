# Z-014 integrated safety SIL

Current-stage system-level host model for the Z-014 watchdog/IWDG migration
boundary. It composes the real portable heartbeat monitor, watchdog policy and
stack-health policy with a deliberately small fake platform mechanism.

It does **not** simulate later ADBMS/CAN/authority stages and does not claim
hardware IWDG evidence. The model keeps BMS_OK and balancing authority absent,
matching the current no-authority build.

Covered classes include exact startup/stale boundaries, partial two-mask
evidence, placeholder non-authority, process-fault versus software-death
separation, proactive stack/integrity fail-closed behavior, irreversible
watchdog start/feed failure semantics, tick wrap, and multi-seed randomized
scheduler/actor interleavings.
