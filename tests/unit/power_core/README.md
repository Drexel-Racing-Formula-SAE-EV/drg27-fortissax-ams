# Z-010 SoP / SoH / fuse core validation

Z-010 ports only the portable power-health algorithms from the frozen DER26
v2.6.27 / firmware 0.5.30 oracle:

- `ams_sop`
- `ams_soh`
- `ams_fuse_observer`

The production source checker reverses the approved include-path changes and
requires SHA-256 equality with the v2.6.27 files.

Host coverage includes:

- the existing 20,000-drive + 20,000-charge SoP metamorphic oracle;
- the existing fuse reference-oracle comparison, including 50,000 randomized
  production/reference states;
- SoH rest-anchor/capacity observability;
- SoH stale/calibration rejection;
- persistence schema/CRC/generation selection;
- resistance-episode median filtering against a transient R0 spike.

`ams_power_state`, `ams_power_strategy`, and `ams_power_can` are intentionally
outside Z-010. They are integration/mission/CAN layers, not part of this
portable algorithm step.
