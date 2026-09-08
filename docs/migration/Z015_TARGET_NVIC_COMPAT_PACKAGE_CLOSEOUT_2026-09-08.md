# Z-015 Target-NVIC-Compatible Package Closeout

Date: 2026-09-08

Status: **host/source/SIL GREEN after pinned-Zephyr v4.4 target-build compatibility correction; fresh STM32F767 base + IWDG target builds remain required.**

This package supersedes the 2026-09-07 HAL-hardened Z-015 package. The first real target compile exposed that the host fakes had invented `CONFIG_ARCH_HAS_IRQ_PENDING_OPS` and `k_irq_clear_pending()`, neither of which is available through pinned Zephyr v4.4.0 for this STM32F767 build. The safety intent is unchanged: SPI6, the shared current-ADC IRQ, and output-only TIM3/4/5 IRQs are disabled and pending-cleared. The implementation now uses the real pinned platform surface: Zephyr `irq_disable()` plus CMSIS `NVIC_ClearPendingIRQ()`.

Compatibility hardening added:

- removed all production dependence on `CONFIG_ARCH_HAS_IRQ_PENDING_OPS`;
- removed all production/test dependence on `k_irq_clear_pending()`;
- host fakes now expose CMSIS `NVIC_ClearPendingIRQ()` through their STM32 SoC seam rather than inventing a Zephyr API;
- source/target contracts reject reintroduction of the nonexistent API/capability;
- Z-015 mutation `zephyr44_nonexistent_pending_api` explicitly proves this guard.

Final compatibility-corrected canonical host/SIL evidence:

- `docs/migration/evidence/Z015_TARGET_NVIC_COMPAT_HOST_SIL_CANONICAL_2026-09-08.log`
- `docs/migration/evidence/Z015_TARGET_NVIC_COMPAT_HOST_SIL_REPORT_2026-09-08.json`
- **54/54 stages passed**;
- **89.231 s**;
- skipped evidence: **none**;
- ThreadSanitizer requested and performed;
- retained Z-014 mutation suite: **18** unsafe mutations rejected;
- expanded Z-015 mutation suite: **47** unsafe mutations rejected.

The package intentionally contains no target build products, no host test binaries/objects, no `build/` directory, and no `__pycache__`/`.pyc` products. No target-build success, flash, physical SPI/ADC/PWM/IWDG validation, authority promotion, or Z-016 work is claimed.

The root `Z015_WORKTREE_SHA256SUMS.txt` is the authoritative per-file source/evidence integrity manifest. The ZIP SHA-256 is reported externally at handoff time.
