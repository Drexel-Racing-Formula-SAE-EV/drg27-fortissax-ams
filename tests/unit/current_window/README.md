# Z-008 current-window host tests

This host-only suite ports the current-window regression contract from the
frozen DER26 v2.6.27 / firmware 0.5.30 oracle into the Zephyr migration tree.

Covered behavior:

- rejection of an old voltage boundary after a newer current sample has already
  crossed it;
- recovery after a tainted out-of-order epoch;
- current integration and exact constant-current charge/average checks;
- tick-wrap-safe time arithmetic;
- maximum 100 ms sample-age and integration-gap handling;
- prevention of a real-sample gap being hidden by a window rotation;
- stale-tail invalidation so an old carried sample cannot charge a new epoch;
- carried uncertainty, extrema, selected range, and calibration provenance;
- sticky mixed-range state until rotation;
- unknown `UINT16_MAX` uncertainty propagation;
- nonfinite current rejection and filtered-current fallback;
- nonzero sequence wrap behavior.

The tests exercise the portable algorithm only. Z-022 later owns the Zephyr
thread/mutex integration that serializes current sample publication with ADBMS
voltage-boundary capture.
