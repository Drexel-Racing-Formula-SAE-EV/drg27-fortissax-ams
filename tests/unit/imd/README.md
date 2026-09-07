# Z-013 IMD portable-core and Zephyr-adapter tests

This directory makes the Z-013 IMD host evidence reproducible from the full
repository. The tests compile the **production** IMD portable core and the
**production** Zephyr capture adapter; the fake Zephyr headers only replace the
kernel/device surface needed to run that production translation unit on a host.

Covered portable-core behavior:

- no capture is never healthy;
- 250 ms capture freshness boundary and tick wrap;
- coherent capture seqlock rejection;
- high-count <= total-count validation;
- full-width 32-bit capture arithmetic;
- 10 Hz status-frequency mapping;
- independent OK_HS semantics;
- capture counter saturation;
- zero clock / stopped capture fail closed.

Covered production-adapter behavior includes:

- PWM/GPIO device-readiness failures;
- exact 108 MHz timer-clock contract;
- GPIO, capture configuration and capture-start failures;
- fail-closed capture-start semantics;
- callback success/error handling;
- wrong-device/wrong-channel callback rejection;
- callback-error recovery after a new clean capture;
- the second callback-fault check that closes a race during GPIO sampling;
- 250/251 ms freshness edges and 32-bit tick wrap;
- invalid capture tuples and OK_HS failure;
- 100,000 deterministic randomized adapter operations.

Run:

```sh
make test
make asan
make analyze
make clean
```

The Z-013 differential validation record also compares the portable IMD core
against the exact v2.6.27 `imd.c` from the frozen oracle package. That oracle
comparison is separate from this fake-Zephyr adapter harness.
