#!/usr/bin/env python3
"""Z018 source and target contract; diagnostics grant no authority."""
from pathlib import Path
import hashlib, re, sys
from check_z017_contract import elf_symbols

def require(ok,msg):
 if not ok: raise SystemExit('FAIL Z018: '+msg)

def source_check(r):
 m=(r/'lib/ams_core/adbms/ams_adbms_monitor.c').read_text()
 p=(r/'lib/ams_core/adbms/ams_adbms_protocol.c').read_text()
 a=(r/'drivers/ams/adbms_monitor_zephyr.c').read_text()
 h=(r/'lib/ams_core/include/ams_core/ams_temp_image.h').read_text()
 image=(r/'lib/ams_core/adbms/ams_temp_image.c').read_text()
 k=(r/'app/Kconfig').read_text()
 def tokens(source,values):
  for v in values:require(v in source,'missing invariant: '+v)
 tokens(h,['AMS_TEMP_COUNT 24U','AMS_TEMP_ALL_MASK 0x00FFFFFFUL','AMS_TEMP_STALE_MS 12000U','AMS_TEMP_MAX_MISSES 10U','AMS_TEMP_JUMP_DECI_C 250U','AMS_TEMP_RATE_DECI_C_PER_S 50U'])
 tokens(image,['THERMISTOR_NOMINAL_VREG_V','t.temperature_c>=-40.0f','t.temperature_c<=150.0f','7*(int32_t)im->filtered_deci_c[i]+v','(n+(n>=0?4:-4))/8','im->last_update_ms[i]=captured[i]','im->misses[i]<=AMS_TEMP_MAX_MISSES','<=AMS_TEMP_STALE_MS','im->valid_mask&=~bit','im->filter_valid_mask&=~bit'])
 tokens(m,['0x4CU+mux','sensor/8U','sensor%8U','comm[6]={0x68U,address,0x08U,data,0x19U,0xFFU}','frame[13]={0}','packet.data[0]!=0x67U','packet.data[2]!=0x07U && packet.data[2]!=0x77U','memcmp(packet.data,comm,6U)!=0','io->write_b(io->context,frame,sizeof(frame))','m->temperature.mux_valid_mask=0U','*raw==INT16_MIN || *raw==(int16_t)-1','mux<3U && result==AMS_ADBMS_RESULT_OK','mux*8U+position','monitor_delay(io,3000U)','4000U,AMS_ADBMS_CMD_RDAUXA','monitor_delay(io,1000U)','(position+1U)%8U','captured[sensor]=now_ms+(uint32_t)((now-start)/1000U)','9000U,AMS_ADBMS_CMD_RDRAXA','d->suspect=d->delta_mv>20;','temporary[2]=0xB8U','6000U,AMS_ADBMS_CMD_RDAUXA','m->temperature.config_cleanup_required=true','m->temperature.config_cleanup_required=false','!muted_cfga_matches_production(packet.data)','!ams_adbms_z017_config_matches(AMS_ADBMS_CMD_RDCFGB,packet.data)','m->state=AMS_ADBMS_MONITOR_FAULTED','m->initialized=false; m->acquisition_live=false; m->config_verified=false','d->delta_mv<10 || d->pullup_delta_mv<10 || d->recovery_delta_mv>20'])
 select=m[m.index('static ams_adbms_result_t temp_select('):m.index('static ams_adbms_result_t temp_capture(')]
 tokens(select,['mux=sensor/8U, position=sensor%8U','m->temperature.mux_valid_mask=0U'])
 primary=m[m.index('ams_adbms_result_t ams_adbms_monitor_temperature('):m.index('static int16_t temp_delta_mv')]
 require('retry' not in primary and 'ams_temp_image_apply(' in primary,'primary retry/publication boundary')
 diagnostics=m[m.index('ams_adbms_result_t ams_adbms_monitor_aux2('):].split('/* Z019 fingerprints',1)[0]
 require('ams_temp_image_apply' not in diagnostics and 'temperature.image' not in diagnostics,'diagnostic primary substitution')
 ow=diagnostics[diagnostics.index('ams_adbms_result_t ams_adbms_monitor_therm_ow'):]
 require(ow.index('config_cleanup_required=true')<ow.index('AMS_ADBMS_CMD_WRCFGA,temporary')<ow.index('AMS_ADBMS_CMD_WRCFGA,production'),'restore ownership ordering')
 tokens(m[m.index('static ams_adbms_result_t temp_prepare('):m.index('static ams_adbms_result_t temp_select(')],['m->temperature.config_cleanup_required','return cleanup_snapshot(m,io)'])
 tokens(a,['k_current_get() != owner_thread','uint32_t temp_now = k_uptime_get_32()','ams_adbms_monitor_temperature(&monitor, &monitor_io, temp_now)','(uint32_t)(temp_now - aux2_last_ms) >= 250U','(uint32_t)(temp_now - ow_last_ms) >= 2000U','aux2_last_ms = temp_now','ow_last_ms = temp_now','COPY(temperature)'])
 for forbidden in ['ams_measurement_publish','ams_watchdog_heartbeat','AMS_ADBMS_SPI_STRING_A']:require(forbidden not in a,'authority/owner leak '+forbidden)
 for symbol in ['AMS_CAP_ADBMS_SAFETY_EVIDENCE','AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE']:
  block=re.search(r'config '+symbol+r'\n(.*?)(?=\nconfig |\nendmenu|\Z)',k,re.S).group(1)
  require('default n' in block and 'default y' not in block,'safety promotion '+symbol)
 block=k.split('config AMS_Z018_TEMP_VALIDATION\n',1)[1].split('\nconfig ',1)[0]
 tokens(block,['depends on !AMS_Z016_LINK_PROBE','depends on !AMS_Z017_CELL_VALIDATION'])
 for symbol in ['AMS_Z018_AUX2_DIAGNOSTIC','AMS_Z018_THERM_OW_DIAGNOSTIC']:
  block=re.search(r'config '+symbol+r'\n(.*?)(?=\nconfig |\nendmenu|\Z)',k,re.S).group(1)
  require('depends on AMS_Z018_TEMP_VALIDATION' in block and 'default n' in block and 'default y' not in block,'diagnostic default/scope')
 for name,aux,ow_enabled in [('z018_temp_validation',False,False),('z018_aux2_validation',True,False),('z018_therm_ow_validation',False,True)]:
  cfg=(r/f'app/{name}.conf').read_text()
  tokens(cfg,['CONFIG_AMS_Z018_TEMP_VALIDATION=y','CONFIG_AMS_Z017_CELL_VALIDATION=n','CONFIG_AMS_Z016_LINK_PROBE=n','CONFIG_AMS_BMS_AUTHORITY=n','CONFIG_AMS_BALANCE_AUTHORITY=n',f'CONFIG_AMS_Z018_AUX2_DIAGNOSTIC={"y" if aux else "n"}',f'CONFIG_AMS_Z018_THERM_OW_DIAGNOSTIC={"y" if ow_enabled else "n"}'])
 for n,b0,b1,kind in [('WRCOMM',7,0x21,'WRITE6'),('RDCOMM',7,0x22,'READ'),('STCOMM',7,0x23,'STCOMM'),('RDAUXA',0,0x19,'READ'),('RDRAXA',0,0x1c,'READ')]+[(n+str(i),b,base+i,'ONLY') for n,b,base in [('ADAX_GPIO',4,0x10),('ADAX2_GPIO',4,0),('ADAX_OW_DOWN_GPIO',5,0x10),('ADAX_OW_UP_GPIO',5,0x90)] for i in range(1,4)]:
  effect='NONE' if kind=='READ' else 'INCREMENT'
  tokens(p,[f'[AMS_ADBMS_CMD_{n}] = {{0x{b0:02X}U, 0x{b1:02X}U, AMS_ADBMS_COMMAND_{kind}, AMS_ADBMS_COUNTER_{effect}}}'])
 tokens((r/'tests/unit/adbms_monitor/Makefile').read_text(),['adapter-z018:','adapter-z018-diag:','thermistor:'])
 # Exact imported model and generated tables; numeric changes require re-freeze.
 for file,digest in MODEL_HASHES.items():
  require(hashlib.sha256((r/file).read_bytes()).hexdigest()==digest,'thermistor oracle drift '+file)
 print('PASS Z018 source contract: primary scan, diagnostics, ownership, restoration, no authority')

def target_check(r,b):
 cfg=(b/'zephyr/.config').read_text().splitlines(); enabled=lambda s:'CONFIG_'+s+'=y' in cfg
 active=enabled('AMS_Z018_TEMP_VALIDATION')
 require(sum(enabled(s) for s in ['AMS_Z016_LINK_PROBE','AMS_Z017_CELL_VALIDATION','AMS_Z018_TEMP_VALIDATION'])<=1,'multiple profiles')
 require(enabled('AMS_CAP_TEMPERATURE_ACQUISITION_LIVE')==active,'temperature capability mismatch')
 for s in ['AMS_BMS_AUTHORITY','AMS_BALANCE_AUTHORITY','AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE','AMS_CAP_ADBMS_SAFETY_EVIDENCE']:require(not enabled(s),'authority/evidence '+s)
 syms=elf_symbols(b/'zephyr/zephyr.elf')
 for symbol,expected in [('ams_adbms_monitor_temperature',active),('ams_adbms_monitor_aux2',enabled('AMS_Z018_AUX2_DIAGNOSTIC')),('ams_adbms_monitor_therm_ow',enabled('AMS_Z018_THERM_OW_DIAGNOSTIC'))]:
  require(not expected or active,'diagnostics outside Z018')
  require((symbol in syms)==expected,'ELF inclusion/exclusion '+symbol)
 print('PASS Z018 target contract')

MODEL_HASHES = {'lib/ams_core/adbms/ams_thermistor.c': '6d6b013f87cb55d461643676c9e32daeaa15dcb2f6187d5bf4932be0b6adf178', 'lib/ams_core/adbms/thermistor_lut_generated.h': '9f84267948e0af3c1977e9ce5e2a6b5a7d623f44306ddf4298643777063b4573', 'lib/ams_core/include/ams_core/ams_thermistor.h': '4c929a531e3a8b7c9cf1a615a334bfe02f1e407d142dd1b0d3d161c8bfb2601f', 'lib/ams_core/include/ams_core/thermistor_model_generated.h': '3bfd73acd0808a614929712d4f03c31ef52dae312977f2891e6c01aa1c41c95c'}  # populated from the imported, frozen v2.6.27 source
if __name__=='__main__':
 r=Path(sys.argv[1]).resolve();source_check(r)
 if len(sys.argv)>2:target_check(r,Path(sys.argv[2]).resolve())
