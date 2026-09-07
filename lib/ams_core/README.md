# AMS portable core

`ams_core` contains platform-neutral DER26/DRG27 AMS behavior and data
contracts that can be exercised by host tests and linked into the Zephyr
application without importing kernel or hardware dependencies.

## Boundary

Code in this directory must not depend on:

- an RTOS kernel or driver API;
- MCU HAL APIs or peripheral registers;
- board pin names;
- dynamic allocation;
- application-thread ownership.

Physical accumulator topology may live here when it is an immutable product
contract rather than a build-profile selection.

## Z-006 foundation

Z-006 established:

- portable scalar types;
- frozen physical topology;
- the `UINT16_MAX` unknown-current-uncertainty sentinel;
- wrap-safe millisecond elapsed-time/freshness helpers.

## Z-007 coherent measurement store

Z-007 ports the frozen reader-pinned double-buffer publication model into the
portable core:

- exactly two static measurement buffers;
- one writer transaction at a time;
- nonzero publication sequences with wrap-safe zero skipping;
- intentional sequence gaps for dropped/aborted writer attempts;
- reader counts that pin a published buffer during copy;
- copy-outside-lock semantics;
- bounded saturating publication-drop accounting;
- explicit writer abort recovery;
- injected short metadata lock operations;
- no heap and no hardware access.

The snapshot carries RAW cell voltage, same-epoch AVG8/IIR candidates,
per-reading ages/usable masks, temperature data, current-window result metadata,
balancing state, and validity flags. RAW cell voltage remains the safety
representation; the filtered/averaged candidates are observational/estimator
inputs only.

The current-window integration and boundary-rotation algorithm is deliberately
left for Z-008 so the v2.6.27 ordering, carried metadata, mixed-range latching,
uncertainty, and calibration-provenance rules can be migrated and tested as one
unit.
