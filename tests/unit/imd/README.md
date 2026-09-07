# Z-013 IMD portable-core tests

Covers the v2.6.27 IMD fail-closed/status/capture contract:

- no capture is never healthy;
- 250 ms capture freshness boundary and tick wrap;
- coherent capture seqlock rejection;
- high-count <= total-count validation;
- full-width 32-bit capture arithmetic;
- 10 Hz status-frequency mapping;
- independent OK_HS semantics;
- capture counter saturation;
- zero clock / stopped capture fail closed.

The Z-013 validation bundle also runs deterministic differential traces against
the exact v2.6.27 `imd.c` from the frozen oracle package.
