AMS_CORE ?= ../../../lib/ams_core
DRIVERS ?= ../../../drivers/ams
CC ?= gcc

COMMON := -std=c11 -Wall -Wextra -Werror -pedantic -O2
CORE_INC := -I$(AMS_CORE)/include
ADC_TEST := ../current_adc
ADC_FAKE_INC := -I$(ADC_TEST)/fake_zephyr -I$(ADC_TEST) -I$(DRIVERS) -I../../../include

.PHONY: all test sensor fault adc asan analyze clean

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
	$(CC) $(COMMON) $(ADC_FAKE_INC) \
		$(DRIVERS)/current_adc_zephyr.c \
		$(ADC_TEST)/fake_zephyr/fake_zephyr_adc.c \
		$(ADC_TEST)/current_adc_adapter_test.c -o current_adc_adapter_test
	./current_adc_adapter_test
	./current_adc_adapter_test low-timeout
	./current_adc_adapter_test ambiguous

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
		$(ADC_FAKE_INC) $(DRIVERS)/current_adc_zephyr.c \
		$(ADC_TEST)/fake_zephyr/fake_zephyr_adc.c \
		$(ADC_TEST)/current_adc_adapter_test.c -o current_adc_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./current_adc_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./current_adc_asan low-timeout
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./current_adc_asan ambiguous

analyze:
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer $(CORE_INC) \
		-c $(AMS_CORE)/current/ams_current_sensor.c -o sensor_an.o
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer $(CORE_INC) \
		-c $(AMS_CORE)/faults/ams_current_fault.c -o fault_an.o
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer $(ADC_FAKE_INC) \
		-c $(DRIVERS)/current_adc_zephyr.c -o adc_an.o

clean:
	rm -f current_sensor_test current_fault_test current_adc_adapter_test \
		current_sensor_asan current_fault_asan current_adc_asan \
		sensor_an.o fault_an.o adc_an.o
