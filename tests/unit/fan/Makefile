AMS_CORE ?= ../../../lib/ams_core
DRIVERS ?= ../../../drivers/ams
CC ?= gcc
CLANG ?= clang
CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -O2

.PHONY: all test asan ubsan analyze clang-analyze clean
all: test

test: fan_control_test fan_pwm_adapter_test
	./fan_control_test
	./fan_pwm_adapter_test

fan_control_test: fan_control_test.c $(AMS_CORE)/fan/ams_fan_control.c
	$(CC) $(CFLAGS) -I$(AMS_CORE)/include $^ -lm -o $@

fan_pwm_adapter_test: fan_pwm_adapter_test.c $(DRIVERS)/fan_pwm_zephyr.c
	$(CC) $(CFLAGS) -Ifake_zephyr -I$(DRIVERS) -I../../../include $^ -lm -o $@

asan:
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -I$(AMS_CORE)/include fan_control_test.c $(AMS_CORE)/fan/ams_fan_control.c -lm -o fan_control_test_asan
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ./fan_control_test_asan
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -Ifake_zephyr -I$(DRIVERS) -I../../../include fan_pwm_adapter_test.c $(DRIVERS)/fan_pwm_zephyr.c -lm -o fan_pwm_adapter_test_asan
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ./fan_pwm_adapter_test_asan

ubsan:
	$(CC) $(CFLAGS) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -I$(AMS_CORE)/include fan_control_test.c $(AMS_CORE)/fan/ams_fan_control.c -lm -o fan_control_test_ubsan
	./fan_control_test_ubsan
	$(CC) $(CFLAGS) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -Ifake_zephyr -I$(DRIVERS) -I../../../include fan_pwm_adapter_test.c $(DRIVERS)/fan_pwm_zephyr.c -lm -o fan_pwm_adapter_test_ubsan
	./fan_pwm_adapter_test_ubsan

analyze:
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer -I$(AMS_CORE)/include \
		-c $(AMS_CORE)/fan/ams_fan_control.c -o fan_core_an.o
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer -Ifake_zephyr -I$(DRIVERS) -I../../../include \
		-c $(DRIVERS)/fan_pwm_zephyr.c -o fan_pwm_an.o

clang-analyze:
	$(CLANG) -std=c11 -Wall -Wextra -Werror --analyze -I$(AMS_CORE)/include $(AMS_CORE)/fan/ams_fan_control.c
	$(CLANG) -std=c11 -Wall -Wextra -Werror --analyze -Ifake_zephyr -I$(DRIVERS) -I../../../include $(DRIVERS)/fan_pwm_zephyr.c

clean:
	rm -f fan_control_test fan_pwm_adapter_test fan_control_test_asan fan_pwm_adapter_test_asan fan_control_test_ubsan fan_pwm_adapter_test_ubsan fan_core_an.o fan_pwm_an.o
	@rm -f *.plist
