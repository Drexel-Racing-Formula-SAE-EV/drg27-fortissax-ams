# Z-007 measurement-store host tests

This host-only test exercises the portable measurement publication contract
without any Zephyr, device-driver, or hardware dependency.

Covered behavior:

- initialization and no-publication state;
- nonzero sequence publication and buffer alternation;
- writer contention/drop behavior and intentional sequence gaps;
- writer abort recovery without disturbing the last publication;
- reader pinning preventing inactive-buffer reuse;
- 32-bit sequence wrap skipping zero;
- saturating publication-drop accounting;
- reader-count saturation refusal;
- wrong-pointer publish rejection and internal-buffer copy refusal;
- concurrent writer/reader coherence stress using a host mutex only in the test.

The current-window integration algorithm is intentionally not part of Z-007;
that is Z-008.
