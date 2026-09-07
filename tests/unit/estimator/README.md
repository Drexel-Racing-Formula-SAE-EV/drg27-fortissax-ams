# Z-009 estimator core host test

This test exercises the portable estimator core copied from the frozen
DER26 AMS v2.6.27 / firmware 0.5.30 oracle.

Covered locally:

- P42A LUT golden values and clamping;
- pack/segment/group-range configuration;
- EKF initialization;
- valid measurement update;
- bad-input rejection;
- dt clamping;
- startup acquisition entry/abort behavior;
- pack/segment/even-split estimator topology;
- coulomb-count reference state;
- summary/status generation;
- basic resistance-observation gating.

The Z-009 validation package also runs a deterministic differential trace
against the exact oracle source from the v2.6.27 ZIP. That differential test is
not stored as a normal repo test because the legacy source is intentionally not
vendored into the Zephyr repository.
