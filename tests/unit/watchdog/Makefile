AMS_CORE ?= ../../../lib/ams_core
CC ?= gcc
CLANG ?= clang
BASE := -std=c11 -Wall -Wextra -Werror -pedantic -I$(AMS_CORE)/include
THREAD_FLAGS ?= -pthread
POLICY_SRC := $(AMS_CORE)/watchdog/ams_watchdog_policy.c $(AMS_CORE)/integrity/ams_stack_health.c
HEARTBEAT_SRC := $(AMS_CORE)/watchdog/ams_watchdog_heartbeat.c
SEEDS := 1 0x12345678 0x00C0FFEE 0xDEADBEEF 0x31415926
OPTS := O0 O1 O2 O3 Os

.PHONY: all test directed heartbeat heartbeat-differential differential adapter concurrency \
	asan ubsan strict analyze clang-analyze tsan clean
all: test

# Canonical focused Z-014 SIL gate.  Keep the exact-oracle differential and
# production-adapter state machine together with the heartbeat/concurrency
# integration evidence; a passing policy unit test alone is not sufficient.
test: directed heartbeat heartbeat-differential differential adapter concurrency

directed:
	$(CC) $(BASE) -O2 watchdog_policy_test.c $(POLICY_SRC) -o watchdog_policy_test
	./watchdog_policy_test

heartbeat:
	$(CC) $(BASE) -O2 watchdog_heartbeat_test.c $(HEARTBEAT_SRC) -o watchdog_heartbeat_test
	./watchdog_heartbeat_test

heartbeat-differential:
	@set -e; total=0; \
	for opt in $(OPTS); do \
		$(CC) $(BASE) -$$opt watchdog_heartbeat_differential.c \
			watchdog_heartbeat_reference_oracle.c $(HEARTBEAT_SRC) \
			-o watchdog_heartbeat_diff_$$opt; \
		for seed in $(SEEDS); do \
			./watchdog_heartbeat_diff_$$opt $$seed 100000; \
			total=$$((total + 100000)); \
		done; \
	done; \
	echo "PASS watchdog heartbeat exact differential: $$total compared operations"

differential:
	@set -e; total=0; \
	for opt in $(OPTS); do \
	  $(CC) $(BASE) -$$opt watchdog_differential.c watchdog_reference_oracle.c \
	    $(AMS_CORE)/watchdog/ams_watchdog_policy.c -o watchdog_diff_$$opt; \
	  for seed in $(SEEDS); do \
	    ./watchdog_diff_$$opt $$seed 100000; total=$$((total + 100000)); \
	  done; \
	done; \
	echo "PASS watchdog exact differential: $$total compared decisions"

adapter:
	$(CC) $(BASE) -O2 -DAMS_WATCHDOG_ADAPTER_TEST -Ifake_zephyr -I../../../include \
		watchdog_adapter_test.c ../../../drivers/ams/watchdog_zephyr.c -o watchdog_adapter_test
	./watchdog_adapter_test

concurrency:
	$(CC) $(BASE) -O2 $(THREAD_FLAGS) watchdog_concurrency_test.c \
		$(AMS_CORE)/watchdog/ams_watchdog_policy.c $(HEARTBEAT_SRC) -o watchdog_concurrency_test
	./watchdog_concurrency_test

asan:
	$(CC) $(BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		watchdog_policy_test.c $(POLICY_SRC) -o watchdog_policy_test_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./watchdog_policy_test_asan
	$(CC) $(BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		watchdog_heartbeat_test.c $(HEARTBEAT_SRC) -o watchdog_heartbeat_test_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./watchdog_heartbeat_test_asan
	$(CC) $(BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		watchdog_heartbeat_differential.c watchdog_heartbeat_reference_oracle.c \
		$(HEARTBEAT_SRC) -o watchdog_heartbeat_diff_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./watchdog_heartbeat_diff_asan 0x12345678 100000
	$(CC) $(BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		watchdog_differential.c watchdog_reference_oracle.c \
		$(AMS_CORE)/watchdog/ams_watchdog_policy.c -o watchdog_diff_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./watchdog_diff_asan 0x12345678 100000
	$(CC) $(BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		-DAMS_WATCHDOG_ADAPTER_TEST -Ifake_zephyr -I../../../include \
		watchdog_adapter_test.c ../../../drivers/ams/watchdog_zephyr.c -o watchdog_adapter_test_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./watchdog_adapter_test_asan
	$(CC) $(BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(THREAD_FLAGS) watchdog_concurrency_test.c \
		$(AMS_CORE)/watchdog/ams_watchdog_policy.c $(HEARTBEAT_SRC) -o watchdog_concurrency_test_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./watchdog_concurrency_test_asan

ubsan:
	$(CC) $(BASE) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		watchdog_policy_test.c $(POLICY_SRC) -o watchdog_policy_test_ubsan
	./watchdog_policy_test_ubsan
	$(CC) $(BASE) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		watchdog_heartbeat_test.c $(HEARTBEAT_SRC) -o watchdog_heartbeat_test_ubsan
	./watchdog_heartbeat_test_ubsan
	$(CC) $(BASE) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		watchdog_heartbeat_differential.c watchdog_heartbeat_reference_oracle.c \
		$(HEARTBEAT_SRC) -o watchdog_heartbeat_diff_ubsan
	./watchdog_heartbeat_diff_ubsan 0x00C0FFEE 100000
	$(CC) $(BASE) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		-DAMS_WATCHDOG_ADAPTER_TEST -Ifake_zephyr -I../../../include \
		watchdog_adapter_test.c ../../../drivers/ams/watchdog_zephyr.c -o watchdog_adapter_test_ubsan
	./watchdog_adapter_test_ubsan
	$(CC) $(BASE) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		$(THREAD_FLAGS) watchdog_concurrency_test.c \
		$(AMS_CORE)/watchdog/ams_watchdog_policy.c $(HEARTBEAT_SRC) -o watchdog_concurrency_test_ubsan
	./watchdog_concurrency_test_ubsan

strict:
	$(CC) $(BASE) -O2 -Wconversion -Wsign-conversion -Wshadow -Wdouble-promotion \
		watchdog_policy_test.c $(POLICY_SRC) -o watchdog_policy_test_strict
	./watchdog_policy_test_strict
	$(CC) $(BASE) -O2 -Wconversion -Wsign-conversion -Wshadow -Wdouble-promotion \
		watchdog_heartbeat_test.c $(HEARTBEAT_SRC) -o watchdog_heartbeat_test_strict
	./watchdog_heartbeat_test_strict

analyze:
	$(CC) $(BASE) -fanalyzer -c $(AMS_CORE)/watchdog/ams_watchdog_policy.c -o watchdog_policy_an.o
	$(CC) $(BASE) -fanalyzer -c $(HEARTBEAT_SRC) -o watchdog_heartbeat_an.o
	$(CC) $(BASE) -fanalyzer -c $(AMS_CORE)/integrity/ams_stack_health.c -o stack_health_an.o
	$(CC) $(BASE) -fanalyzer -DAMS_WATCHDOG_ADAPTER_TEST -Ifake_zephyr -I../../../include \
		-c ../../../drivers/ams/watchdog_zephyr.c -o watchdog_adapter_an.o

clang-analyze:
	$(CLANG) $(BASE) --analyze $(AMS_CORE)/watchdog/ams_watchdog_policy.c
	$(CLANG) $(BASE) --analyze $(HEARTBEAT_SRC)
	$(CLANG) $(BASE) --analyze $(AMS_CORE)/integrity/ams_stack_health.c
	$(CLANG) $(BASE) --analyze -DAMS_WATCHDOG_ADAPTER_TEST -Ifake_zephyr -I../../../include \
		../../../drivers/ams/watchdog_zephyr.c

# Optional race detector.  This is intentionally separate from `test`: some
# CI/container runtimes cannot reserve ThreadSanitizer shadow memory.  A host
# that supports TSan should run it as additional evidence.
tsan:
	$(CC) $(BASE) -O1 -g -fsanitize=thread $(THREAD_FLAGS) \
		watchdog_concurrency_test.c $(AMS_CORE)/watchdog/ams_watchdog_policy.c \
		$(HEARTBEAT_SRC) -o watchdog_concurrency_test_tsan
	TSAN_OPTIONS=halt_on_error=1 ./watchdog_concurrency_test_tsan

clean:
	rm -f watchdog_policy_test watchdog_policy_test_asan watchdog_policy_test_ubsan \
		watchdog_heartbeat_test watchdog_heartbeat_test_asan watchdog_heartbeat_test_ubsan \
		watchdog_heartbeat_diff_* \
		watchdog_concurrency_test watchdog_concurrency_test_asan watchdog_concurrency_test_ubsan \
		watchdog_concurrency_test_tsan watchdog_diff_* \
		watchdog_policy_an.o watchdog_heartbeat_an.o stack_health_an.o \
		watchdog_adapter_test watchdog_adapter_test_asan watchdog_adapter_test_ubsan \
		watchdog_policy_test_strict watchdog_heartbeat_test_strict watchdog_adapter_an.o
	@rm -f *.plist
