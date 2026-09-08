AMS_CORE ?= ../../../lib/ams_core
DRIVERS ?= ../../../drivers/ams
CC ?= gcc
CLANG ?= clang

COMMON := -std=c11 -Wall -Wextra -Werror -pedantic -O2
CORE_INC := -I$(AMS_CORE)/include
ADC_TEST := ../current_adc
ADC_FAKE_INC := -I$(ADC_TEST)/fake_zephyr -I$(ADC_TEST) -I$(DRIVERS) -I../../../include
ADC_SRC := $(DRIVERS)/current_adc_stm32.c
ADC_FAKE := $(ADC_TEST)/fake_zephyr/fake_zephyr_adc.c
ADC_TEST_SRC := $(ADC_TEST)/current_adc_adapter_test.c
ADC_DEFS := -DAMS_CURRENT_ADC_ADAPTER_TEST=1

.PHONY: all test sensor fault adc asan ubsan analyze clang-analyze clean

all: test

test: sensor fault adc

sensor:
	$(CC) $(COMMON) $(CORE_INC) \
		$(AMS_CORE)/current/ams_current_sensor.c \
		../current_sensor/current_sensor_test.c -lm -o current_sensor_test
	./current_sensor_test

fault:
	$(CC) $(COMMON) $(CORE_INC) \
		$(AMS_CORE)/faults/ams_current_fault.c \
		../current_fault/current_fault_test.c -lm -o current_fault_test
	./current_fault_test

adc:
	$(CC) $(COMMON) $(ADC_DEFS) $(ADC_FAKE_INC) \
		$(ADC_SRC) $(ADC_FAKE) $(ADC_TEST_SRC) -o current_adc_adapter_test
	./current_adc_adapter_test
	./current_adc_adapter_test high-timeout
	./current_adc_adapter_test low-timeout
	./current_adc_adapter_test timeout-boundary
	./current_adc_adapter_test recovery-fail
	./current_adc_adapter_test reentry
	./current_adc_adapter_test stress

asan:
	$(CC) $(COMMON) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(CORE_INC) $(AMS_CORE)/current/ams_current_sensor.c \
		../current_sensor/current_sensor_test.c -lm -o current_sensor_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./current_sensor_asan
	$(CC) $(COMMON) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(CORE_INC) $(AMS_CORE)/faults/ams_current_fault.c \
		../current_fault/current_fault_test.c -lm -o current_fault_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./current_fault_asan
	$(CC) $(COMMON) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(ADC_DEFS) $(ADC_FAKE_INC) $(ADC_SRC) $(ADC_FAKE) $(ADC_TEST_SRC) -o current_adc_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./current_adc_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./current_adc_asan high-timeout
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./current_adc_asan low-timeout
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./current_adc_asan timeout-boundary
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./current_adc_asan recovery-fail
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./current_adc_asan reentry

ubsan:
	$(CC) $(COMMON) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		$(CORE_INC) $(AMS_CORE)/current/ams_current_sensor.c \
		../current_sensor/current_sensor_test.c -lm -o current_sensor_ubsan
	./current_sensor_ubsan
	$(CC) $(COMMON) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		$(CORE_INC) $(AMS_CORE)/faults/ams_current_fault.c \
		../current_fault/current_fault_test.c -lm -o current_fault_ubsan
	./current_fault_ubsan
	$(CC) $(COMMON) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		$(ADC_DEFS) $(ADC_FAKE_INC) $(ADC_SRC) $(ADC_FAKE) $(ADC_TEST_SRC) -o current_adc_ubsan
	./current_adc_ubsan
	./current_adc_ubsan high-timeout
	./current_adc_ubsan low-timeout
	./current_adc_ubsan timeout-boundary
	./current_adc_ubsan recovery-fail
	./current_adc_ubsan reentry

analyze:
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer $(CORE_INC) \
		-c $(AMS_CORE)/current/ams_current_sensor.c -o sensor_an.o
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer $(CORE_INC) \
		-c $(AMS_CORE)/faults/ams_current_fault.c -o fault_an.o
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer $(ADC_DEFS) $(ADC_FAKE_INC) \
		-c $(ADC_SRC) -o adc_an.o

clang-analyze:
	$(CLANG) -std=c11 -Wall -Wextra -Werror --analyze $(CORE_INC) $(AMS_CORE)/current/ams_current_sensor.c
	$(CLANG) -std=c11 -Wall -Wextra -Werror --analyze $(CORE_INC) $(AMS_CORE)/faults/ams_current_fault.c
	$(CLANG) -std=c11 -Wall -Wextra -Werror --analyze $(ADC_DEFS) $(ADC_FAKE_INC) $(ADC_SRC)

clean:
	rm -f current_sensor_test current_fault_test current_adc_adapter_test \
		current_sensor_asan current_fault_asan current_adc_asan \
		current_sensor_ubsan current_fault_ubsan current_adc_ubsan \
		sensor_an.o fault_an.o adc_an.o
	@rm -f *.plist
