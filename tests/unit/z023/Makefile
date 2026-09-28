CC ?= cc
ROOT := ../../..
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -I$(ROOT)/lib/ams_core/include
CORE := $(ROOT)/lib/ams_core/faults/ams_supervision.c $(ROOT)/lib/ams_core/faults/ams_segment_fault.c
ADAPTER := $(ROOT)/drivers/ams/supervision_zephyr.c
INC := -I../z022/fake -I$(ROOT)/include
.PHONY: test sanitize analyze
ORACLE := oracle/voltage_fault.c oracle/temperature_fault.c
differential:
	$(CC) $(FLAGS) -Ioracle -O2 differential_test.c $(CORE) $(ORACLE) -o differential_test
	./differential_test
concurrency:
	$(CC) $(FLAGS) -Iconcurrent_fake -I$(ROOT)/include -O2 concurrency_test.c $(CORE) $(ADAPTER) -pthread -o concurrency_test
	./concurrency_test
tsan:
	$(CC) $(FLAGS) -Iconcurrent_fake -I$(ROOT)/include -O1 -g -fsanitize=thread concurrency_test.c $(CORE) $(ADAPTER) -pthread -o concurrency_tsan
	./concurrency_tsan
test:
	$(CC) $(FLAGS) -O2 segment_fault_test.c $(CORE) -o segment_fault_test
	./segment_fault_test
	$(CC) $(FLAGS) -O2 supervision_test.c $(CORE) -o supervision_test
	./supervision_test
	$(CC) $(FLAGS) $(INC) -O2 adapter_test.c $(CORE) $(ADAPTER) -o adapter_test
	./adapter_test
sanitize:
	$(CC) $(FLAGS) -O1 -g -fsanitize=address,undefined segment_fault_test.c $(CORE) -o segment_fault_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./segment_fault_sanitize
	$(CC) $(FLAGS) -O1 -g -fsanitize=address,undefined supervision_test.c $(CORE) -o supervision_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./supervision_sanitize
	$(CC) $(FLAGS) $(INC) -O1 -g -fsanitize=address,undefined adapter_test.c $(CORE) $(ADAPTER) -o adapter_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./adapter_sanitize
analyze:
	$(CC) $(FLAGS) -fanalyzer -c $(ROOT)/lib/ams_core/faults/ams_supervision.c -o supervision.o
	$(CC) $(FLAGS) -fanalyzer -c $(ROOT)/lib/ams_core/faults/ams_segment_fault.c -o segment_fault.o
	$(CC) $(FLAGS) $(INC) -fanalyzer -c $(ADAPTER) -o adapter.o

EST := $(ROOT)/lib/ams_core/estimator
MEAS := $(ROOT)/lib/ams_core/measurement
CURRENT := $(MEAS)/ams_measurement_pipeline.c $(MEAS)/ams_current_window.c $(MEAS)/ams_measurement.c $(ROOT)/lib/ams_core/current/ams_current_sensor.c $(ROOT)/lib/ams_core/faults/ams_current_fault.c $(EST)/ams_segment_consumer.c $(EST)/ams_soc_ekf.c $(EST)/ams_estimator_lut.c $(ROOT)/drivers/ams/measurement_pipeline_zephyr.c
FAKE := ../adbms_spi/fake_zephyr
INTEGRATION := integration_test.c $(filter-out $(ROOT)/drivers/ams/measurement_pipeline_zephyr.c,$(CURRENT)) $(CORE) $(ADAPTER) $(ROOT)/app/src/ams_z023_safety.c
INTEGRATION_FLAGS := $(FLAGS) $(INC) -I$(ROOT)/app/src -DCONFIG_AMS_Z023_SUPERVISION_VALIDATION=1 -DAMS_TEST_MUTEX_FAILURE=1
integration:
	$(CC) $(INTEGRATION_FLAGS) -O2 $(INTEGRATION) -lm -o integration_test
	./integration_test
integration-sanitize:
	$(CC) $(INTEGRATION_FLAGS) -O1 -g -fsanitize=address,undefined $(INTEGRATION) -lm -o integration_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./integration_sanitize
DIAGNOSTICS ?= 0
MON_FLAGS := -DCONFIG_AMS_Z022_MEASUREMENT_VALIDATION=1 -DCONFIG_AMS_Z019_RECOVERY_VALIDATION=1 -DCONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION=1 -DCONFIG_AMS_Z018_TEMP_VALIDATION=1 -DCONFIG_AMS_Z018_AUX2_DIAGNOSTIC=$(DIAGNOSTICS) -DCONFIG_AMS_Z018_THERM_OW_DIAGNOSTIC=$(DIAGNOSTICS)
hooks:
	$(CC) $(FLAGS) $(INC) -DCONFIG_AMS_Z023_SUPERVISION_VALIDATION=1 -O2 current_hook_test.c $(CURRENT) $(CORE) $(ADAPTER) -lm -o current_hook_test
	./current_hook_test
	$(CC) $(FLAGS) -I$(FAKE) -I$(ROOT)/include -I$(ROOT)/drivers/ams $(MON_FLAGS) -DCONFIG_AMS_Z023_SUPERVISION_VALIDATION=1 -O2 monitor_hook_test.c $(CORE) $(ADAPTER) $(ROOT)/lib/ams_core/adbms/ams_balance_shadow.c $(FAKE)/fake_zephyr_spi.c $(ROOT)/drivers/ams/adbms_time_zephyr.c $(ROOT)/drivers/ams/adbms_monitor_zephyr.c -o monitor_hook_test
	./monitor_hook_test
