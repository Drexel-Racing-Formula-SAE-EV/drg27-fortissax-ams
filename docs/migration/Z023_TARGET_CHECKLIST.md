# Z023 target and physical qualification

Host/SIL completion is not target or physical completion. Keep BMS_OK and
balancing authority disabled in every image below. Temperature hardware/pull-ups
are already validated; these are checks of the migrated firmware.

## Target builds

From the repository in an initialized Zephyr workspace:

```powershell
$profiles = @('', 'z014_watchdog_validation.conf', 'z016_isospi_probe.conf',
  'z017_cell_validation.conf', 'z018_temp_validation.conf',
  'z018_aux2_validation.conf', 'z018_therm_ow_validation.conf',
  'z019_recovery_validation.conf', 'z020_balance_disabled_validation.conf',
  'z022_measurement_validation.conf', 'z023_supervision_validation.conf')
foreach ($profile in $profiles) {
  $label = if ($profile) { $profile.Replace('.conf','') } else { 'base' }
  $build = "build/z023_matrix_$label"
  if ($profile) { west build -p always -b der26_ams app -d $build -- "-DEXTRA_CONF_FILE=$profile" }
  else { west build -p always -b der26_ams app -d $build }
  if ($LASTEXITCODE -ne 0) { throw "Build failed: $label" }
  py scripts/check_all_contracts.py . $build
  if ($LASTEXITCODE -ne 0) { throw "Contracts failed: $label" }
}
```

Also compile Z023 with AUX2 and thermistor-OW enabled independently, then
together, using a local validation overlay. Do not change the default profile.
Check thread stacks/RAM, profile symbol exclusion and all evidence/authority
flags. Run `check_null_platform_core.py` with CMake and the available Clang
analysis targets; these were unavailable during the host closeout.

## Bench evidence

- Confirm String B, one SMB, 15 cells, 24 temperatures and 100/20-ms owner cadence.
- Measure worker completion to supervisor commit, scan duration, skipped releases,
  lock wait bounds, publication drops and maximum watchdog gaps. No catch-up burst.
- Compare raw-C OV/UV, 60/65-C thermal debounce and startup/precharge current
  classification with a controlled known-good baseline; never generate unsafe
  physical cell conditions merely to test a threshold—use safe injection.
- Interrupt/disconnect the monitor link; verify cleanup debt, recovery generation,
  first terminal outcome and no repeated progress from cached terminal returns.
- Freeze each owner and delay the supervisor independently. Verify stale data,
  preserved latches/history and fail-low. Verify all eight positions are required
  for TEMP progress and recovery invalidates partial coverage.
- Exercise optional diagnostics separately before combining them. Diagnostic
  samples must never replace primary temperatures; restoration remains mandatory.
- Verify physical BMS_OK low and balancing inactive throughout. Record calibration,
  polarity, uncertainty and sensor range provenance; do not fabricate confidence.

Promote CURRENT/ADBMS/TEMP safety evidence only through a separate reviewed,
actor-specific qualification change. CAN remains unqualified and required;
full coverage stays false. Full-pack/mixed-ring and vehicle authority remain out
of scope for this single-SMB validation image.
