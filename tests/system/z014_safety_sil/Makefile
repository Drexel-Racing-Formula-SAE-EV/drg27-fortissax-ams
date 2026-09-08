AMS_CORE ?= ../../../lib/ams_core
CC ?= gcc
CLANG ?= clang
BASE := -std=c11 -Wall -Wextra -Werror -pedantic -I$(AMS_CORE)/include
SRC := z014_safety_sil.c \
	$(AMS_CORE)/watchdog/ams_watchdog_policy.c \
	$(AMS_CORE)/watchdog/ams_watchdog_heartbeat.c \
	$(AMS_CORE)/integrity/ams_stack_health.c

.PHONY: all test strict asan ubsan analyze clang-analyze clean
all: test

test: z014_safety_sil
	./z014_safety_sil

z014_safety_sil: $(SRC)
	$(CC) $(BASE) -O2 $(SRC) -o $@

strict:
	$(CC) $(BASE) -O2 -Wconversion -Wsign-conversion -Wshadow -Wdouble-promotion \
		$(SRC) -o z014_safety_sil_strict
	./z014_safety_sil_strict

asan:
	$(CC) $(BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(SRC) -o z014_safety_sil_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./z014_safety_sil_asan

ubsan:
	$(CC) $(BASE) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		$(SRC) -o z014_safety_sil_ubsan
	./z014_safety_sil_ubsan

analyze:
	$(CC) $(BASE) -fanalyzer -c z014_safety_sil.c -o z014_safety_sil_an.o

clang-analyze:
	$(CLANG) $(BASE) --analyze -Xanalyzer -analyzer-output=text z014_safety_sil.c

clean:
	rm -f z014_safety_sil z014_safety_sil_strict z014_safety_sil_asan \
		z014_safety_sil_ubsan z014_safety_sil_an.o *.plist
