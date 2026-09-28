#!/usr/bin/env python3
"""Z017 String-B one-ADBMS6830 initialization/acquisition contract.

With only repo_root, enforces source/profile architecture. With an optional
build_dir, also proves the generated .config/map/ELF profile composition.
"""
from __future__ import annotations
import re, struct, sys
from pathlib import Path


def fail(msg: str) -> None:
    print("FAIL: Z017 " + msg); raise SystemExit(1)

def require(cond: bool, msg: str) -> None:
    if not cond: fail(msg)

def sym_enabled(cfg: str, sym: str) -> bool:
    return re.search(rf"^{re.escape(sym)}=y$", cfg, re.M) is not None

def elf_symbols(path: Path) -> set[str]:
    data=path.read_bytes(); require(data[:4]==b"\x7fELF", "target is not ELF")
    cls,endian_id=data[4],data[5]; endian="<" if endian_id==1 else ">"
    require(cls in (1,2) and endian_id in (1,2),"unsupported ELF")
    if cls==1:
        eh=endian+"HHIIIIIHHHHHH"; sh=endian+"IIIIIIIIII"; sy=endian+"IIIBBH"
    else:
        eh=endian+"HHIQQQIHHHHHH"; sh=endian+"IIQQQQIIQQ"; sy=endian+"IBBHQQ"
    e=struct.unpack_from(eh,data,16); shoff,shentsz,shnum=int(e[5]),int(e[10]),int(e[11])
    secs=[tuple(int(x) for x in struct.unpack_from(sh,data,shoff+i*shentsz)) for i in range(shnum)]
    out=set()
    for sec in secs:
        if sec[1] not in (2,11): continue
        off,size,link,entsz=sec[4],sec[5],sec[6],sec[9]
        st=secs[link]; strings=data[st[4]:st[4]+st[5]]
        for pos in range(off,off+size,entsz):
            x=struct.unpack_from(sy,data,pos); name=int(x[0]); shndx=int(x[5] if cls==1 else x[3])
            if not name or shndx==0 or name>=len(strings): continue
            end=strings.find(b"\0",name)
            if end>name: out.add(strings[name:end].decode(errors="replace"))
    require(bool(out),"ELF symbol table unavailable")
    return out


def source_check(repo: Path) -> None:
    protocol_h=(repo/'lib/ams_core/include/ams_core/ams_adbms_protocol.h').read_text()
    protocol=(repo/'lib/ams_core/adbms/ams_adbms_protocol.c').read_text()
    monitor_h=(repo/'lib/ams_core/include/ams_core/ams_adbms_monitor.h').read_text()
    monitor=(repo/'lib/ams_core/adbms/ams_adbms_monitor.c').read_text()
    image=(repo/'lib/ams_core/adbms/ams_cell_image.c').read_text()
    adapter=(repo/'drivers/ams/adbms_monitor_zephyr.c').read_text()
    platform_h=(repo/'include/ams_platform/adbms_monitor.h').read_text()
    threads=(repo/'app/src/ams_threads.c').read_text()
    kconfig=(repo/'app/Kconfig').read_text()
    conf=(repo/'app/z017_cell_validation.conf').read_text()

    # Frozen topology/profile/no-authority boundary.
    for tok in ('AMS_ADBMS_Z017_STRING_B 1U','AMS_ADBMS_Z017_IC_COUNT 1U',
                'AMS_ADBMS_Z017_MONITORED_CELL_COUNT 15U','AMS_ADBMS_Z017_MONITORED_CELL_MASK 0x7FFFU'):
        require(tok in protocol_h,"topology constant drift: "+tok)
    for tok in ('CONFIG_AMS_Z017_CELL_VALIDATION=y','CONFIG_AMS_Z016_LINK_PROBE=n',
                'CONFIG_AMS_BMS_AUTHORITY=n','CONFIG_AMS_BALANCE_AUTHORITY=n'):
        require(tok in conf,"profile configuration missing: "+tok)
    require('depends on !AMS_Z016_LINK_PROBE' in kconfig.split("config AMS_Z017_CELL_VALIDATION\n",1)[1].split("\nconfig ",1)[0],"Z016/Z017 mutual exclusion removed")
    require('default y if AMS_Z017_CELL_VALIDATION || AMS_Z018_TEMP_VALIDATION' in kconfig,"conditional Z017 capability promotion missing")

    # Exact production vectors/timing/current-board branch.
    for tok in ('AMS_ADBMS_Z017_CFGA0 0x81U','AMS_ADBMS_Z017_CFGA3 0xFFU','AMS_ADBMS_Z017_CFGA5 0x03U',
                'AMS_ADBMS_Z017_CFGB0 0x71U','AMS_ADBMS_Z017_CFGB1 0x52U','AMS_ADBMS_Z017_CFGB2 0x46U',
                'AMS_ADBMS_Z017_REFERENCE_PREWAIT_US 3000U','AMS_ADBMS_Z017_REDUNDANT_WAIT_US 17000U',
                'AMS_ADBMS_Z017_SNAPSHOT_SETTLE_US 10U','AMS_ADBMS_Z017_MAX_POST_ATTEMPTS 2U',
                'AMS_ADBMS_Z017_MAX_EPOCH_ATTEMPTS 2U','AMS_ADBMS_Z017_IIR_FC 3U'):
        require(tok in protocol_h or tok in monitor_h,"frozen Z017 constant missing: "+tok)
    for tok in ('[AMS_ADBMS_CMD_ADCV_Z017]    = {0x03U, 0xE0U',
                '[AMS_ADBMS_CMD_ADCV_BASELINE]= {0x03U, 0x64U'):
        require(tok in protocol,"ADCV golden vector drift: "+tok)

    inventory=re.findall(r'^\s*(AMS_ADBMS_CMD_\w+)\s*[,=]',protocol_h,re.M)
    require(len(inventory)==len(set(inventory)), 'duplicate command enum')
    # Startup-only balancing-safe subset: MUTE/zero/readback, absolutely no UNMUTE.
    safe_cmds={
        'AMS_ADBMS_CMD_MUTE':'0x28U','AMS_ADBMS_CMD_WRPWMA':'0x20U','AMS_ADBMS_CMD_WRPWMB':'0x21U',
        'AMS_ADBMS_CMD_RDPWMA':'0x22U','AMS_ADBMS_CMD_RDPWMB':'0x23U'}
    for cmd,byte in safe_cmds.items():
        require(cmd in protocol_h and f'[{cmd}]' in protocol and byte in protocol,
                f"startup inhibit command missing/drifted: {cmd}")
    require('AMS_ADBMS_CMD_UNMUTE' not in protocol_h and '0x29U, AMS_ADBMS_' not in protocol,
            "UNMUTE is forbidden in Z017 command inventory")
    for forbidden in ('ADBMS2950','APM_'):
        require(forbidden not in protocol_h,"forbidden later-stage protocol surfaced: "+forbidden)
    for tok in ('startup_balance_inhibit','AMS_ADBMS_CMD_MUTE','muted_cfga_matches_production',
                'AMS_ADBMS_CMD_WRCFGB','AMS_ADBMS_CMD_WRPWMA','AMS_ADBMS_CMD_WRPWMB',
                'AMS_ADBMS_CMD_RDCFGB','AMS_ADBMS_CMD_RDPWMA','AMS_ADBMS_CMD_RDPWMB',
                'balance_mute_verified','balance_durable_zero_verified','best_effort_balance_zero'):
        require(tok in monitor,"startup balance-inhibit parity missing: "+tok)
    init=monitor[monitor.find('ams_adbms_result_t ams_adbms_monitor_initialize'):
                 monitor.find('static void clear_candidate')]
    post_pos=init.find('result = run_post(m, io);')
    final_verify_pos=init.find('result = verify_config(m, io);', post_pos + 1)
    inhibit_pos=init.find('result = startup_balance_inhibit(m, io);', final_verify_pos + 1)
    require(0 <= post_pos < final_verify_pos < inhibit_pos,
            "startup must run POST -> final exact config readback -> balance inhibit")
    require(re.search(r'\bphysical_validated\s*=\s*true\b', monitor+adapter) is None,
            "Z017 source fabricated physical validation")

    # SNAP ownership: obligation precedes transport, known remote state precedes clock marking.
    ss=monitor[monitor.find('static ams_adbms_result_t session_command'):
               monitor.find('static ams_adbms_result_t session_read')]
    pre=ss.find('m->snapshot_cleanup_required = true;'); wire=ss.find('io->write_b(')
    active=ss.find('m->snapshot_active = true;',wire); mark=ss.rfind('session_mark_activity')
    clear=ss.find('m->snapshot_cleanup_required = false;',wire)
    require(0 <= pre < wire < active < mark,"SNAP cleanup obligation/remote-state ordering unsafe")
    require(wire < clear < mark,"known-complete UNSNAP cleanup must be recorded before clock marking")
    require('if (m->snapshot_cleanup_required)' in monitor and 'cleanup_snapshot(m, io)' in monitor,
            "outstanding snapshot cleanup is not a precondition to a new epoch")

    # Epoch/current-board sequence, bounded retry, no ADCV in retry loop.
    acq=monitor[monitor.find('ams_adbms_result_t ams_adbms_monitor_acquire'):
                monitor.find('void ams_adbms_monitor_snapshot')]
    require(acq.find('AMS_ADBMS_Z017_REFERENCE_PREWAIT_US') < acq.find('AMS_ADBMS_CMD_ADCV_Z017') <
            acq.find('AMS_ADBMS_Z017_REDUNDANT_WAIT_US') < acq.find('for (uint8_t attempt = 1U'),
            "current-board ADCV/retry sequence drift")
    retry=acq[acq.find('for (uint8_t attempt = 1U'):]
    require('AMS_ADBMS_CMD_ADCV_Z017' not in retry,"whole-epoch retry incorrectly reissues ADCV")
    read_epoch=monitor[monitor.find('static ams_adbms_result_t read_epoch'):monitor.find('ams_adbms_result_t ams_adbms_monitor_acquire')]
    ordered=('AMS_ADBMS_CMD_SNAP','raw_commands','AMS_ADBMS_CMD_RDSTATC','AMS_ADBMS_CMD_RDSTATD','avg_commands','iir_commands')
    last=-1
    for tok in ordered:
        pos=read_epoch.find(tok); require(pos>last,"epoch order drift at "+tok); last=pos
    require(read_epoch.rfind('cleanup_snapshot') > last,
            "successful/failed epoch does not finish through UNSNAP cleanup")
    require('statc.ccts != 0U' in read_epoch,"CCTS nonzero proof removed")
    require('ams_cell_image_apply_raw(&m->cells, m->raw_codes' in read_epoch,
            "raw C no longer feeds raw authoritative cell image")
    require('ams_cell_image_apply_avg8(&m->cells, m->avg8_codes' in read_epoch,
            "AVG8 product source drift/fallback")
    require('ams_cell_image_apply_iir(&m->cells, m->iir_codes' in read_epoch,
            "IIR product source drift/fallback")
    require('ams_cell_image_apply_raw(&m->cells, m->avg8_codes' not in read_epoch and
            'ams_cell_image_apply_raw(&m->cells, m->iir_codes' not in read_epoch,
            "optional product substituted for raw C authority")

    # IIR values may be diagnostic before ready, but usable mask must not be.
    iir=image[image.find('void ams_cell_image_apply_iir'):]
    require('image->iir_usable_mask = 0U;' in iir,"IIR pre-readiness usable mask is not cleared")
    require('image->iir_usable_mask = usable;' not in iir,
            "IIR filtered data is exposed usable before readiness")
    ready=iir.find('image->iir_ready = true;'); publish=iir.find('image->iir_usable_mask = (uint16_t)(usable & AMS_CELL_IMAGE_MONITORED_MASK);')
    require(0 <= ready < publish,"IIR usable mask can publish before readiness")
    require('AMS_CELL_IMAGE_IIR_MIN_EPOCH_GAP_MS' in iir,"100-ms IIR readiness qualification missing")

    # Diagnostic names must say stage-mask, not imply legacy IC masks.
    require('post_failed_mask' not in monitor_h+platform_h+monitor+adapter,
            "ambiguous legacy POST failed-mask name remains")
    require('post_unexpected_mask' not in monitor_h+platform_h+monitor+adapter,
            "ambiguous legacy POST unexpected-mask name remains")
    require('post_failed_stage_mask' in platform_h and 'post_unexpected_stage_mask' in platform_h,
            "stage-qualified POST diagnostics missing")

    # Owner/platform boundary: private String B only, no raw opcode/direction API.
    require('AMS_ADBMS_SPI_STRING_B' in adapter and 'AMS_ADBMS_SPI_STRING_A' not in adapter,
            "Z017 adapter is not String-B-only")
    for bad in ('ams_adbms_spi_write(', 'ams_adbms_spi_write_read(', 'ams_adbms_command_t', 'AMS_ADBMS_CMD_'):
        require(bad not in platform_h,"raw transport/opcode leaked into public monitor API: "+bad)
    require('k_is_in_isr()' in adapter and 'ams_adbms_spi_bind_owner()' in adapter,
            "owner/ISR boundary missing")

    # Existing priority-3 / 100-ms owner and no safety-heartbeat promotion.
    require('#define AMS_PRIO_ADBMS        3' in threads and '#define AMS_PERIOD_ADBMS_MS       100U' in threads,
            "ADBMS priority/period drift")
    require('CONFIG_AMS_Z017_CELL_VALIDATION' in threads and 'ams_adbms_monitor_platform_step((uint32_t)start_ms)' in threads,
            "Z017 monitor not dispatched by existing ADBMS owner")
    require('watchdog_heartbeat_kick(AMS_WATCHDOG_HEARTBEAT_ADBMS' not in threads,
            "Z017 acquisition fabricated ADBMS safety heartbeat")
    for forbidden in ('ams_measurement_publish','ams_current_window_rotate','ams_soc_','ams_sop_','ams_soh_'):
        require(forbidden not in adapter,"Z017 prematurely integrated later-stage authority: "+forbidden)
    require('static bool z017_init_attempted;' in threads and
            'if (!z017_init_attempted)' in threads and 'else if (z017_ready)' in threads,
            "Z017 owner lost one-shot initialization/no-reinit boundary")
    require('do {' in threads and 'next_release_ms += thread->period_ms;' in threads and
            '} while (complete_ms >= next_release_ms);' in threads,
            "ADBMS owner no longer skips missed releases after overrun")

    # Required host campaign structure must remain in-tree; this prevents a
    # later cleanup from silently deleting the fault-injection coverage while
    # leaving only the source checker green.
    monitor_test=(repo/'tests/unit/adbms_monitor/monitor_test.c').read_text()
    for tok in ('test_every_startup_transfer_fault','test_every_acquisition_read_fault',
                'test_epoch_expiry_across_mandatory_groups','test_snapshot_ownership_faults',
                'UINT8_MAX','50000'):
        require(tok in monitor_test,"required Z017 host fault/boundary test missing: "+tok)

    print('PASS: Z017 source contract (String-B one-SMB init/POST/safe-inhibit/coherent C acquisition)')


def target_check(repo: Path, build: Path) -> None:
    for rel in ('zephyr/.config','zephyr/zephyr.map','zephyr/zephyr.elf'):
        require((build/rel).is_file(),"missing target artifact: "+rel)
    cfg=(build/'zephyr/.config').read_text(errors='replace')
    mp=(build/'zephyr/zephyr.map').read_text(errors='replace')
    syms=elf_symbols(build/'zephyr/zephyr.elf')
    z017 = sym_enabled(cfg, 'CONFIG_AMS_Z017_CELL_VALIDATION')
    z018 = sym_enabled(cfg, "CONFIG_AMS_Z018_TEMP_VALIDATION")
    require(not (z017 and z018), "Z017/Z018 profiles cannot coexist")
    z017 = z017 or z018
    z016 = sym_enabled(cfg, 'CONFIG_AMS_Z016_LINK_PROBE')
    require(not (z016 and z017), "Z016 and Z017 linked together")

    # No profile may promote authority/safety/physical claims at this stage.
    for sym in ('CONFIG_AMS_BMS_AUTHORITY','CONFIG_AMS_BALANCE_AUTHORITY',
                'CONFIG_AMS_CAP_ADBMS_SAFETY_EVIDENCE',
                'CONFIG_AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE',
                'CONFIG_AMS_CAP_ADBMS_SPI_PHYSICAL_VALIDATED'):
        require(not sym_enabled(cfg,sym), "forbidden target authority/evidence: " + sym)

    if not z017:
        require(not sym_enabled(cfg, 'CONFIG_AMS_CAP_ADBMS_MONITOR_ACQUISITION_LIVE'),
                "non-Z017 target fabricated monitor-acquisition capability")
        require('adbms_monitor_zephyr.c.obj' not in mp,
                "Z017 platform adapter linked outside Z017 profile")
        for sym in ('ams_adbms_monitor_platform_init_owner',
                    'ams_adbms_monitor_platform_step',
                    'ams_adbms_monitor_platform_snapshot'):
            require(sym not in syms, "Z017 platform symbol leaked outside Z017 profile: " + sym)
        print('PASS: Z017 target exclusion contract (profile inactive)')
        return

    require(not z016, "Z017 target also enabled Z016 probe")
    for sym in ('CONFIG_AMS_CAP_ADBMS_ACTOR_LIVE',
                'CONFIG_AMS_CAP_ADBMS_MONITOR_ACQUISITION_LIVE'):
        require(sym_enabled(cfg,sym), "Z017 target capability missing: " + sym)
    for sym in ('ams_adbms_monitor_platform_init_owner','ams_adbms_monitor_platform_step',
                'ams_adbms_monitor_platform_snapshot','ams_adbms_monitor_initialize',
                'ams_adbms_monitor_acquire','ams_adbms_spi_write',
                'ams_adbms_spi_write_read','ams_adbms_spi_wake_b'):
        require(sym in syms, "linked Z017 symbol missing: " + sym)
    require('ams_adbms_link_probe_step' not in syms,
            "Z016 finite probe symbol leaked into Z017 ELF")
    require('adbms_monitor_zephyr.c.obj' in mp,
            "Z017 platform adapter object absent from map")
    require('adbms_link_probe.c.obj' not in mp,
            "Z016 probe object linked in Z017 map")
    print('PASS: Z017 target ELF/profile contract')


def main() -> int:
    if len(sys.argv) not in (2,3): raise SystemExit('usage: check_z017_contract.py REPO [BUILD]')
    repo=Path(sys.argv[1]).resolve(); source_check(repo)
    if len(sys.argv)==3: target_check(repo,Path(sys.argv[2]).resolve())
    return 0

if __name__=='__main__': sys.exit(main())
