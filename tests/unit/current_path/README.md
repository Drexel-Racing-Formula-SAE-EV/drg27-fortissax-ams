# Z-011 current-path host gate

`make test` runs the directed portable current-sensor tests, directed
current-fault timing tests, and the real Zephyr ADC adapter compiled against the
fake Zephyr SIL surface. The ADC adapter test contains 50,000 randomized
transactions in addition to directed failure/timeout cases.

`make asan` runs the same path under AddressSanitizer and UndefinedBehaviorSanitizer.
`make analyze` runs GCC `-fanalyzer` on the three production translation units.

The 5-million-operation current-sensor/current-fault differential test requires
the frozen v2.6.27 oracle and is intentionally run by the migration validation
workflow rather than vendoring the legacy firmware into this repository.

The two `*_differential_trace.c` harnesses are stored here so the exact parity
run is reproducible when the frozen v2.6.27 source tree is available. Compile
each once with `-DORACLE` against the legacy source/stubs and once against the
portable `ams_core` source, then compare the printed deterministic trace hash
for the same seed/operation count. The Z-011 validation used 100,000 operations,
5 seeds, and five optimization levels for both harnesses (5 million compared
operations total).
