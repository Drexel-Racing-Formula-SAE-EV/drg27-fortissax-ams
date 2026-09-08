APP_SRC ?= ../../../app/src
CC ?= gcc
CLANG ?= clang
BASE := -std=c11 -Wall -Wextra -Werror -pedantic -Ifake_zephyr -I$(APP_SRC) -I../../../include
SRC := safety_fatal_test.c $(APP_SRC)/ams_safety.c

.PHONY: all test strict asan ubsan analyze clang-analyze clean
all: test

test: safety_fatal_test
	./safety_fatal_test

safety_fatal_test: $(SRC)
	$(CC) $(BASE) -O2 $(SRC) -o $@

strict:
	$(CC) $(BASE) -O2 -Wconversion -Wsign-conversion -Wshadow $(SRC) -o safety_fatal_test_strict
	./safety_fatal_test_strict

asan:
	$(CC) $(BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(SRC) -o safety_fatal_test_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./safety_fatal_test_asan

ubsan:
	$(CC) $(BASE) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all $(SRC) -o safety_fatal_test_ubsan
	./safety_fatal_test_ubsan

analyze:
	$(CC) $(BASE) -fanalyzer -c $(APP_SRC)/ams_safety.c -o ams_safety_an.o

clang-analyze:
	$(CLANG) $(BASE) --analyze -Xanalyzer -analyzer-output=text $(APP_SRC)/ams_safety.c

clean:
	rm -f safety_fatal_test safety_fatal_test_strict safety_fatal_test_asan \
		safety_fatal_test_ubsan ams_safety_an.o *.plist
