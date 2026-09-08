CC ?= cc
ROOT := ../../..
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -Wno-error=pedantic -I$(ROOT)/lib/ams_core/include -Ioracle $(EXTRA)
SOURCES := link_test.c oracle/adbms_shared.c $(ROOT)/lib/ams_core/adbms/ams_adbms_link.c
.PHONY: test asan clean
test:
	$(CC) $(FLAGS) -O2 $(SOURCES) -o link_test
	./link_test
asan:
	$(CC) $(FLAGS) -O1 -g -fsanitize=address,undefined $(SOURCES) -o link_test_asan
	ASAN_OPTIONS=detect_leaks=0 ./link_test_asan
clean:
	rm -f link_test link_test_asan adapter_test probe_test

FAKE := ../adbms_spi/fake_zephyr
DRIVERS := $(ROOT)/drivers/ams
ADAPTER_FLAGS := $(FLAGS) -DCONFIG_AMS_Z016_LINK_PROBE=1 -I$(FAKE) -I$(DRIVERS) -I$(ROOT)/include
ADAPTER_SOURCES := adapter_test.c $(FAKE)/fake_zephyr_spi.c $(DRIVERS)/adbms_spi_engine.c $(DRIVERS)/adbms_spi_stm32.c $(DRIVERS)/adbms_time_zephyr.c
.PHONY: adapter
adapter:
	$(CC) $(ADAPTER_FLAGS) -O2 $(ADAPTER_SOURCES) -o adapter_test
	./adapter_test
	./adapter_test stalled
	./adapter_test cs-fault

.PHONY: probe
probe:
	$(CC) $(ADAPTER_FLAGS) -O2 probe_test.c $(DRIVERS)/adbms_link_probe.c $(ROOT)/lib/ams_core/adbms/ams_adbms_link.c -o probe_test
	./probe_test
	./probe_test bad-pec
