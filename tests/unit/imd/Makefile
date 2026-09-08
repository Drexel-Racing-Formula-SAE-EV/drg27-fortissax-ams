AMS_CORE ?= ../../../lib/ams_core
DRIVERS ?= ../../../drivers/ams
CC ?= gcc
CLANG ?= clang
CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -O2

.PHONY: all test core adapter asan ubsan analyze clang-analyze clean
all: test

test: core adapter

core:
	$(CC) $(CFLAGS) -I$(AMS_CORE)/include imd_core_test.c \
		$(AMS_CORE)/imd/ams_imd.c -lm -o imd_core_test
	./imd_core_test

adapter:
	$(CC) $(CFLAGS) -Ifake_zephyr -I$(DRIVERS) -I../../../include -I$(AMS_CORE)/include \
		imd_capture_adapter_test.c $(DRIVERS)/imd_capture_zephyr.c \
		$(AMS_CORE)/imd/ams_imd.c -lm -o imd_capture_adapter_test
	./imd_capture_adapter_test

asan:
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(AMS_CORE)/include imd_core_test.c $(AMS_CORE)/imd/ams_imd.c \
		-lm -o imd_core_test_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./imd_core_test_asan
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		-Ifake_zephyr -I$(DRIVERS) -I../../../include -I$(AMS_CORE)/include \
		imd_capture_adapter_test.c $(DRIVERS)/imd_capture_zephyr.c \
		$(AMS_CORE)/imd/ams_imd.c -lm -o imd_capture_adapter_test_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./imd_capture_adapter_test_asan

ubsan:
	$(CC) $(CFLAGS) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		-I$(AMS_CORE)/include imd_core_test.c $(AMS_CORE)/imd/ams_imd.c \
		-lm -o imd_core_test_ubsan
	./imd_core_test_ubsan
	$(CC) $(CFLAGS) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		-Ifake_zephyr -I$(DRIVERS) -I../../../include -I$(AMS_CORE)/include \
		imd_capture_adapter_test.c $(DRIVERS)/imd_capture_zephyr.c \
		$(AMS_CORE)/imd/ams_imd.c -lm -o imd_capture_adapter_test_ubsan
	./imd_capture_adapter_test_ubsan

analyze:
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer -Ifake_zephyr \
		-I$(DRIVERS) -I../../../include -I$(AMS_CORE)/include -c $(DRIVERS)/imd_capture_zephyr.c \
		-o imd_adapter_an.o
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer -I$(AMS_CORE)/include \
		-c $(AMS_CORE)/imd/ams_imd.c -o imd_core_an.o

clang-analyze:
	$(CLANG) -std=c11 -Wall -Wextra -Werror --analyze -Ifake_zephyr -I$(DRIVERS) -I../../../include -I$(AMS_CORE)/include $(DRIVERS)/imd_capture_zephyr.c
	$(CLANG) -std=c11 -Wall -Wextra -Werror --analyze -I$(AMS_CORE)/include $(AMS_CORE)/imd/ams_imd.c

clean:
	rm -f imd_core_test imd_capture_adapter_test \
		imd_core_test_asan imd_capture_adapter_test_asan \
		imd_core_test_ubsan imd_capture_adapter_test_ubsan \
		imd_adapter_an.o imd_core_an.o
	@rm -f *.plist
