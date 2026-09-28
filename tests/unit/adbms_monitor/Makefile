CC ?= cc
ROOT := ../../..
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -I$(ROOT)/lib/ams_core/include $(EXTRA)
SOURCES := monitor_test.c \
 $(ROOT)/lib/ams_core/adbms/ams_adbms_protocol.c \
 $(ROOT)/lib/ams_core/adbms/ams_cell_image.c \
 $(ROOT)/lib/ams_core/adbms/ams_temp_image.c \
 $(ROOT)/lib/ams_core/adbms/ams_thermistor.c \
 $(ROOT)/lib/ams_core/adbms/ams_adbms_monitor.c
.PHONY: test asan ubsan analyze clang-analyze clean
test:
	$(CC) $(FLAGS) -O2 $(SOURCES) -lm -o monitor_test
	./monitor_test
asan:
	$(CC) $(FLAGS) -O1 -g -fsanitize=address -fno-omit-frame-pointer $(SOURCES) -lm -o monitor_test_asan
	ASAN_OPTIONS=detect_leaks=0 ./monitor_test_asan
ubsan:
	$(CC) $(FLAGS) -O1 -g -fsanitize=undefined -fno-omit-frame-pointer $(SOURCES) -lm -o monitor_test_ubsan
	./monitor_test_ubsan
analyze:
	@set -e; for f in ams_adbms_monitor ams_adbms_protocol ams_temp_image ams_thermistor; do $(CC) $(FLAGS) -O2 -fanalyzer -c $(ROOT)/lib/ams_core/adbms/$$f.c -o /tmp/z018_$$f.o; done
clang-analyze:
	@set -e; for f in ams_adbms_monitor ams_adbms_protocol ams_temp_image ams_thermistor; do clang --analyze $(FLAGS) -O1 $(ROOT)/lib/ams_core/adbms/$$f.c -o /tmp/z018_$$f.plist; done
clean:
	rm -f adapter_z018_test adapter_z018_diag_test thermistor_test monitor_test monitor_test_asan monitor_test_ubsan adapter_test /tmp/z017_monitor_analyze.o /tmp/z017_monitor.plist

FAKE := ../adbms_spi/fake_zephyr
DRIVERS := $(ROOT)/drivers/ams
ADAPTER_FLAGS := $(FLAGS) -DCONFIG_AMS_Z017_CELL_VALIDATION=1 -DCONFIG_AMS_Z016_LINK_PROBE=0 -I$(FAKE) -I$(DRIVERS) -I$(ROOT)/include
.PHONY: adapter
adapter:
	$(CC) $(ADAPTER_FLAGS) -O2 adapter_test.c $(FAKE)/fake_zephyr_spi.c $(DRIVERS)/adbms_time_zephyr.c $(DRIVERS)/adbms_monitor_zephyr.c -o adapter_test
	./adapter_test

Z018_FLAGS := $(FLAGS) -DCONFIG_AMS_Z018_TEMP_VALIDATION=1 -I$(FAKE) -I$(DRIVERS) -I$(ROOT)/include
adapter-z018:
	$(CC) $(Z018_FLAGS) -DCONFIG_AMS_Z018_AUX2_DIAGNOSTIC=0 -DCONFIG_AMS_Z018_THERM_OW_DIAGNOSTIC=0 -O2 adapter_test.c $(FAKE)/fake_zephyr_spi.c $(DRIVERS)/adbms_time_zephyr.c $(DRIVERS)/adbms_monitor_zephyr.c -o adapter_z018_test
	./adapter_z018_test
adapter-z018-diag:
	$(CC) $(Z018_FLAGS) -DCONFIG_AMS_Z018_AUX2_DIAGNOSTIC=1 -DCONFIG_AMS_Z018_THERM_OW_DIAGNOSTIC=1 -O2 adapter_test.c $(FAKE)/fake_zephyr_spi.c $(DRIVERS)/adbms_time_zephyr.c $(DRIVERS)/adbms_monitor_zephyr.c -o adapter_z018_diag_test
	./adapter_z018_diag_test
thermistor:
	$(CC) $(FLAGS) -O2 thermistor_oracle_test.c $(ROOT)/lib/ams_core/adbms/ams_thermistor.c -lm -o thermistor_test
	./thermistor_test

adapter-z019:
	$(CC) $(Z018_FLAGS) -DCONFIG_AMS_Z019_RECOVERY_VALIDATION=1 -DCONFIG_AMS_Z018_AUX2_DIAGNOSTIC=0 -DCONFIG_AMS_Z018_THERM_OW_DIAGNOSTIC=0 -O2 adapter_test.c $(FAKE)/fake_zephyr_spi.c $(DRIVERS)/adbms_time_zephyr.c $(DRIVERS)/adbms_monitor_zephyr.c -o adapter_z019_test
	./adapter_z019_test

adapter-z019-diag:
	$(CC) $(Z018_FLAGS) -DCONFIG_AMS_Z019_RECOVERY_VALIDATION=1 -DCONFIG_AMS_Z018_AUX2_DIAGNOSTIC=1 -DCONFIG_AMS_Z018_THERM_OW_DIAGNOSTIC=1 -O2 adapter_test.c $(FAKE)/fake_zephyr_spi.c $(DRIVERS)/adbms_time_zephyr.c $(DRIVERS)/adbms_monitor_zephyr.c -o adapter_z019_diag_test
	./adapter_z019_diag_test

Z020_SOURCE := $(ROOT)/lib/ams_core/adbms/ams_balance_shadow.c
adapter-z020:
	$(CC) $(Z018_FLAGS) -DCONFIG_AMS_Z019_RECOVERY_VALIDATION=1 -DCONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION=1 -DCONFIG_AMS_Z018_AUX2_DIAGNOSTIC=0 -DCONFIG_AMS_Z018_THERM_OW_DIAGNOSTIC=0 -O2 adapter_test.c $(Z020_SOURCE) $(FAKE)/fake_zephyr_spi.c $(DRIVERS)/adbms_time_zephyr.c $(DRIVERS)/adbms_monitor_zephyr.c -o adapter_z020_test
	./adapter_z020_test
balance-shadow:
	$(CC) $(FLAGS) -O2 balance_shadow_test.c $(Z020_SOURCE) -o balance_shadow_test
	./balance_shadow_test
balance-shadow-sanitize:
	$(CC) $(FLAGS) -O1 -g -fsanitize=address,undefined balance_shadow_test.c $(Z020_SOURCE) -o balance_shadow_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./balance_shadow_sanitize
balance-shadow-analyze:
	$(CC) $(FLAGS) -O2 -fanalyzer -c $(Z020_SOURCE) -o /tmp/z020_balance_shadow.o

balance-shadow-clang:
	clang --analyze $(FLAGS) -O1 $(Z020_SOURCE) -o /tmp/z020_balance_shadow.plist

adapter-z022:
	$(CC) $(Z018_FLAGS) -DCONFIG_AMS_Z022_MEASUREMENT_VALIDATION=1 -DCONFIG_AMS_Z019_RECOVERY_VALIDATION=1 -DCONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION=1 -DCONFIG_AMS_Z018_AUX2_DIAGNOSTIC=0 -DCONFIG_AMS_Z018_THERM_OW_DIAGNOSTIC=0 -O2 adapter_test.c $(Z020_SOURCE) $(FAKE)/fake_zephyr_spi.c $(DRIVERS)/adbms_time_zephyr.c $(DRIVERS)/adbms_monitor_zephyr.c -o adapter_z022_test
	./adapter_z022_test
