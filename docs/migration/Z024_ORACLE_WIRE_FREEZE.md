# Z024 oracle and initial wire freeze

Baseline: DER26-AMS-MiL v2.6.27, frozen source provided with this migration.
`tests/unit/z024/oracle/can_tx_scheduler.c` and `.h` are unmodified source copies.
The production scheduler differs only in its public include path. Differential
builds rename symbols at compile time; they do not rewrite oracle behavior.

Scheduler C SHA-256:
`9fe7b7ad86bac0c888e2e58b19b5686cf0e25e1cb7ce9347b15ed4ca7f291503`

Task C SHA-256:
`75c883321c438d00551afb012b0d566dae2c9f3e9e148c7bc09d09eb08b8564e`

The original ECU, logger and scheduler-fix documents are copied under
`docs/migration/oracle/z024/`. They describe the original system; copying them
is not a claim that this checkpoint implements all those messages.

## Initial message inventory

| ID | Wire format / timing | Contents | Z024 checkpoint disposition |
|---|---|---|---|
| 0x680 | Standard, DLC8, target 100 ms | Version, rolling group sequence, raw state, validity/fault/authority bits | Withheld; receiver authority interpretation requires verification |
| 0x681 | Standard, DLC8, target 100 ms | Big-endian pack decivolts, signed deciamps, min/max cell mV | Withheld; a 15-cell segment cannot represent a valid 75-cell pack |
| 0x682 | Standard, DLC8, target 100 ms | Signed deci-C temperature summaries, fan %, thermal flags | Withheld; full-pack validity and sentinel mapping remain to be ported |
| 0x683 | Standard, DLC8, target 100 ms | Segment/cell/sensor locations and usable counts | Withheld pending coherent bundle integration |
| 0x684–0x687 | Required protected set with 0x680–0x683 | Original power bundle | Withheld; no SoP/torque authority |
| 0x689/0x68A | Protected advisory in original scheduler integration | Original advisory power fields | Withheld |
| 0x68B | Standard, DLC8, target 100 ms | Current provenance; see table below | Portable codec and isolated test seam only; no live TX |
| 0x690–0x6C0 | Original read-only detail stream; default detail period 500 ms | Detailed logger/tuning snapshots | Withheld; no producer integration |
| Charger commands | Original control messages | Charger state control | Excluded from Z024 |

### Frozen 0x68B non-APM encoding

| Bytes | Field | Rule |
|---|---|---|
| 0 | Source | 0 unavailable, 1 DHAB; APM source 2 excluded |
| 1 | Quality | 0 invalid, 1 valid/unproven calibration, 2 confidently calibrated |
| 2 | Boundary valid | 1 iff source valid and sample age <=100 ms |
| 3 | Source epoch | Literal 1, as in oracle; not a recovery cookie |
| 4–5 | Sample sequence | Low 16 bits, big-endian; diagnostic rolling value |
| 6–7 | Sample age | Milliseconds, big-endian; valid range here 0–100 |

Invalid samples clear bytes 0–2 and 4–7. This preserves the original zero-age
invalid encoding; consumers must inspect validity rather than interpreting zero
age alone as freshness. Timestamp arithmetic uses unsigned 32-bit differences.
Input is one copied current-window provenance record. No live ADC fallback is
introduced. No saturation is needed in the valid path because age <=100 and the
sequence is explicitly truncated to 16 bits. Golden tests cover these bytes.

Queue residence is added before transport and receive publication; data beyond
100 ms is discarded. This is documented hardening, not byte-for-byte timing parity
with a legacy task that encodes immediately before submission.

### Timing evidence still requiring target confirmation

Original `Core/Inc/app.h` defaults to 1000 kbit/s: prescaler 3, SJW 2 TQ,
BS1 15 TQ, BS2 2 TQ. This is 18 TQ/bit and an 88.89% sample point, requiring
54 MHz peripheral clock for 1 Mbit/s. Alternate macros select prescalers 6/12
for 500/250 kbit/s. These are original source settings, not newly verified
Zephyr clock or electrical measurements. Current board DTS declares CAN1
PD0 RX / PD1 TX and leaves it disabled. Pin/transceiver and installed target
clock checks remain required before either live profile is introduced.
