DRIVERS ?= ../../../drivers/ams
CC ?= gcc
CLANG ?= clang
BASE := -std=c11 -Wall -Wextra -Werror -pedantic -I$(DRIVERS)
ENGINE := $(DRIVERS)/adbms_spi_engine.c
OPTS := O0 O1 O2 O3 Os
SEEDS := 1 0x12345678 0x00C0FFEE 0xDEADBEEF 0x31415926

.PHONY: all test directed randomized adapter strict asan ubsan analyze clang-analyze clean
all: test

test: directed randomized adapter

directed:
	$(CC) $(BASE) -O2 adbms_spi_engine_test.c $(ENGINE) -o adbms_spi_engine_test
	./adbms_spi_engine_test

# Exact production-engine model campaign: 5 optimization levels x 200k
# operations = 1,000,000 stateful transport operations per canonical run.
randomized:
	@set -e; total=0; i=0; \
	for opt in $(OPTS); do \
		seed=$$(echo $(SEEDS) | cut -d' ' -f$$((i+1))); \
		$(CC) $(BASE) -$$opt adbms_spi_engine_randomized.c $(ENGINE) -o adbms_spi_random_$$opt; \
		./adbms_spi_random_$$opt $$seed 200000; \
		total=$$((total+200000)); i=$$((i+1)); \
	done; \
	echo "PASS Z-015 ADBMS SPI optimization/model campaign: $$total operations"


ADAPTER_BASE := $(BASE) -Ifake_zephyr -I../../../include
ADAPTER_SRCS := adbms_spi_adapter_test.c fake_zephyr/fake_zephyr_spi.c $(ENGINE) $(DRIVERS)/adbms_spi_stm32.c
ADAPTER_SCENARIOS := nominal tx-timeout rx-timeout bsy-timeout fault cs-assert recovery-fail reentry clock-bad pinctrl-bad reset-init-bad

adapter:
	$(CC) $(ADAPTER_BASE) -O2 $(ADAPTER_SRCS) -o adbms_spi_adapter_test
	@set -e; for s in $(ADAPTER_SCENARIOS); do ./adbms_spi_adapter_test $$s; done
	./adbms_spi_adapter_test random 25000

strict:
	$(CC) $(BASE) -O2 -Wconversion -Wsign-conversion -Wshadow \
		adbms_spi_engine_test.c $(ENGINE) -o adbms_spi_engine_test_strict
	./adbms_spi_engine_test_strict
	$(CC) $(ADAPTER_BASE) -O2 -Wconversion -Wsign-conversion -Wshadow $(ADAPTER_SRCS) -o adbms_spi_adapter_test_strict
	./adbms_spi_adapter_test_strict nominal

asan:
	$(CC) $(BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		adbms_spi_engine_test.c $(ENGINE) -o adbms_spi_engine_test_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./adbms_spi_engine_test_asan
	$(CC) $(BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		adbms_spi_engine_randomized.c $(ENGINE) -o adbms_spi_random_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./adbms_spi_random_asan 0xA5A5A5A5 100000
	$(CC) $(ADAPTER_BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(ADAPTER_SRCS) -o adbms_spi_adapter_test_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./adbms_spi_adapter_test_asan random 10000

ubsan:
	$(CC) $(BASE) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		adbms_spi_engine_test.c $(ENGINE) -o adbms_spi_engine_test_ubsan
	./adbms_spi_engine_test_ubsan
	$(CC) $(BASE) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		adbms_spi_engine_randomized.c $(ENGINE) -o adbms_spi_random_ubsan
	./adbms_spi_random_ubsan 0x5A5A5A5A 100000
	$(CC) $(ADAPTER_BASE) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all $(ADAPTER_SRCS) -o adbms_spi_adapter_test_ubsan
	./adbms_spi_adapter_test_ubsan random 10000

analyze:
	$(CC) $(BASE) -fanalyzer -c $(ENGINE) -o adbms_spi_engine_an.o
	$(CC) $(ADAPTER_BASE) -fanalyzer -c $(DRIVERS)/adbms_spi_stm32.c -o adbms_spi_adapter_an.o

clang-analyze:
	$(CLANG) $(BASE) --analyze $(ENGINE)
	$(CLANG) $(ADAPTER_BASE) --analyze $(DRIVERS)/adbms_spi_stm32.c

clean:
	rm -f adbms_spi_engine_test adbms_spi_engine_test_strict adbms_spi_engine_test_asan \
		adbms_spi_engine_test_ubsan adbms_spi_random_* adbms_spi_engine_an.o \
		adbms_spi_adapter_test adbms_spi_adapter_test_strict adbms_spi_adapter_test_asan \
		adbms_spi_adapter_test_ubsan adbms_spi_adapter_an.o *.plist
