#!/usr/bin/env python3
"""Source and optional generated-profile gate for the restricted Z016 link.

Z017 shares the protocol PEC/counter primitives, but the Z016 executable profile
must remain a finite String-B/one-SMB read-only probe. This checker therefore
verifies the profile-facing allowlist rather than freezing an obsolete private
implementation spelling.
"""
import sys
from pathlib import Path


def check(repo: Path, build: Path | None = None) -> None:
    def require(ok: bool, msg: str) -> None:
        if not ok:
            raise SystemExit("FAIL: Z016 " + msg)

    core = (repo / "lib/ams_core/adbms/ams_adbms_link.c").read_text()
    header = (repo / "lib/ams_core/include/ams_core/ams_adbms_link.h").read_text()
    protocol = (repo / "lib/ams_core/adbms/ams_adbms_protocol.c").read_text()
    probe = (repo / "drivers/ams/adbms_link_probe.c").read_text()
    target = (repo / "drivers/ams/adbms_spi_stm32.c").read_text()
    timing = (repo / "drivers/ams/adbms_time_zephyr.c").read_text()
    kconfig = (repo / "app/Kconfig").read_text()

    for token in ("AMS_ADBMS_LINK_STRING 1U", "AMS_ADBMS_LINK_IC_COUNT 1U",
                  "AMS_ADBMS_LINK_GUARD_US 3000U"):
        require(token in header, "profile changed: " + token)
    require("typedef enum { AMS_LINK_SID, AMS_LINK_CFGA } ams_link_read_t;" in header,
            "read-only public allowlist changed")
    require("ams_adbms_result_t (*now_us)" in header and
            "ams_adbms_result_t (*wake)" in header and
            "ams_adbms_result_t (*read)" in header,
            "typed link outcomes regressed")
    require("AMS_ADBMS_CMD_RDSID : AMS_ADBMS_CMD_RDCFGA" in core,
            "SID/CFGA command allowlist drift")
    require("ams_adbms_build_command_frame(command, cmd)" in core,
            "shared command-frame builder removed")
    require("ams_adbms_decode_packet(rx, &link->counter, &packet)" in core,
            "shared PEC/counter decoder removed")
    require("if (guarded)" in core and "AMS_LINK_SESSION_EXPIRED" in core,
            "coherent-session expiry softened")
    require("return ams_adbms_pec15(data, length);" in core and
            "return ams_adbms_pec10(data, counter);" in core,
            "Z016 no longer uses the single protocol PEC source")
    require("AMS_ADBMS_CMD_RDSID" in protocol and "AMS_ADBMS_CMD_RDCFGA" in protocol,
            "shared protocol lost Z016 read commands")

    require("AMS_ADBMS_SPI_STRING_B,cmd,4U,rx,8U" in probe,
            "physical String-B/count contract changed")
    require("ams_adbms_spi_write(" not in probe,
            "Z016 probe gained a write path")
    require("if (result!=AMS_LINK_OK) step=4U;" in probe,
            "probe no longer stops after first failure")
    require("string != AMS_ADBMS_SPI_STRING_B" in target,
            "String A exclusion removed from live-profile transport")
    require("link_owner==k_current_get()" in target,
            "owner identity missing")
    require("if (!link_owner_valid())" in target,
            "wake ownership check missing")
    require("for (uint32_t i=0; i<budget; ++i)" in timing,
            "timing finite-iteration bound removed")
    require("k_cycle_get_64()" in timing, "64-bit timebase removed")
    require("depends on !AMS_Z017_CELL_VALIDATION" in kconfig,
            "Z016/Z017 mutual exclusion removed")

    for name in ("app/prj.conf", "app/z016_isospi_probe.conf"):
        text = (repo / name).read_text()
        for flag in ("AMS_BMS_AUTHORITY", "AMS_BALANCE_AUTHORITY",
                     "AMS_ADBMS_SPI_PHYSICAL_VALIDATED"):
            require("CONFIG_" + flag + "=y" not in text,
                    "unqualified authority/physical claim")
    z016_conf = (repo / "app/z016_isospi_probe.conf").read_text()
    require("CONFIG_AMS_Z016_LINK_PROBE=y" in z016_conf,
            "explicit probe profile disabled")
    require("CONFIG_AMS_Z017_CELL_VALIDATION=y" not in z016_conf,
            "Z017 enabled in Z016 probe profile")

    for path in (repo / "app").rglob("*.c"):
        if path.name != "ams_threads.c":
            require("ams_adbms_link_probe_step(" not in path.read_text(),
                    "probe called outside owner thread")
    threads = (repo / "app/src/ams_threads.c").read_text()
    require("if (thread == &threads[AMS_THREAD_ADBMS])" in threads,
            "probe not restricted to ADBMS descriptor")

    if build is not None:
        cfg = (build / "zephyr/.config").read_text().splitlines()
        if "CONFIG_AMS_Z016_LINK_PROBE=y" in cfg:
            for flag in ("CORTEX_M_SYSTICK_64BIT_CYCLE_COUNTER",
                         "TIMER_HAS_64BIT_CYCLE_COUNTER"):
                require("CONFIG_" + flag + "=y" in cfg,
                        "missing timing option " + flag)
            require("CONFIG_PM=y" not in cfg, "unqualified power management")
            require("CONFIG_AMS_Z017_CELL_VALIDATION=y" not in cfg and "CONFIG_AMS_Z018_TEMP_VALIDATION=y" not in cfg,
                    "Z016/Z017 simultaneously enabled")
    print("PASS: Z016 String B/one-SMB read-only profile, typed ownership/integrity/timing contract")


if __name__ == "__main__":
    check(Path(sys.argv[1]), Path(sys.argv[2]) if len(sys.argv) > 2 else None)
