# Z-015 Target-Contract Comment-Filter Package Closeout

Date: 2026-09-08

Status: **host/source/SIL GREEN; both STM32F767 base and IWDG images compile/link GREEN; complete target contract rerun remains required after this checker-only correction.**

The first target builds of the Zephyr-v4.4 NVIC-compatible package both linked successfully. The complete contract suite then stopped in `check_current_adc_contract.py` because that checker scanned raw source text and interpreted the word `adc_context` inside an oracle-provenance comment in `drivers/ams/current_adc_stm32.c` as an executable ownership path. The production current-ADC implementation contains no `adc_context` use; the failure was entirely in the checker.

This package fixes the checker rather than weakening the architecture rule:

- `check_current_adc_contract.py` now strips C/C++ comments before scanning forbidden executable ownership/blocking paths;
- the real source still rejects executable `adc_context`, `adc_read_async`, `k_poll_signal`, `k_yield`, `k_sleep`, IRQ enable/connect, heap ownership, and related forbidden paths;
- unavailable Zephyr-v4.4 pending-IRQ API checks are also applied to executable code rather than comments;
- `check_current_adc_contract_host_selftest.py` proves comment-only forbidden names are ignored while real executable `adc_context` and `k_yield()` remain detectable;
- the canonical Z-015 host runner now requires that checker regression.

No production C, Devicetree, Kconfig, linker, or target-build input changed in this correction. Therefore the already-produced base and IWDG target ELFs do not need to be rebuilt solely for this checker update; the complete contract suite can be rerun against those existing build directories.

Target compile evidence supplied externally by the target host before this checker-only correction:

- base image linked successfully: FLASH 162876 B / 2 MiB (7.77%), RAM 45824 B / 384 KiB (11.65%);
- IWDG image linked successfully: FLASH 162932 B / 2 MiB (7.77%), RAM 45824 B / 384 KiB (11.65%).

These compile results are not embedded as target build products in this source package. Full target-green status is still withheld until `check_all_contracts.py` completes successfully on both existing build directories.

Final checker-corrected canonical host/SIL evidence:

- `docs/migration/evidence/Z015_TARGET_CONTRACT_COMMENT_FILTER_HOST_SIL_CANONICAL_2026-09-08.log`
- `docs/migration/evidence/Z015_TARGET_CONTRACT_COMMENT_FILTER_HOST_SIL_REPORT_2026-09-08.json`
- **56/56 stages passed**;
- **88.090 s**;
- skipped evidence: **none**;
- ThreadSanitizer requested and performed;
- retained Z-014 mutation suite: **18** unsafe mutations rejected;
- Z-015 mutation suite: **47** unsafe mutations rejected.

No physical SPI/ADC/PWM/IWDG validation, authority promotion, or Z-016 work is claimed.

The root `Z015_WORKTREE_SHA256SUMS.txt` is the authoritative per-file package manifest. The external ZIP SHA-256 is reported at handoff time.
