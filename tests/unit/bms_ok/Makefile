DRIVERS ?= ../../../drivers/ams
PLATFORM_INC ?= ../../../include
CC ?= gcc
CLANG ?= clang
CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -O2
INCLUDES := -Ifake_zephyr -I$(PLATFORM_INC)

.PHONY: all test asan ubsan analyze clang-analyze clean
all: test

test: bms_ok_adapter_test
	./bms_ok_adapter_test

bms_ok_adapter_test: bms_ok_adapter_test.c $(DRIVERS)/bms_ok_zephyr.c
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

asan:
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(INCLUDES) bms_ok_adapter_test.c $(DRIVERS)/bms_ok_zephyr.c \
		-o bms_ok_adapter_test_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./bms_ok_adapter_test_asan

ubsan:
	$(CC) $(CFLAGS) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
		$(INCLUDES) bms_ok_adapter_test.c $(DRIVERS)/bms_ok_zephyr.c \
		-o bms_ok_adapter_test_ubsan
	./bms_ok_adapter_test_ubsan

analyze:
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer $(INCLUDES) \
		-c $(DRIVERS)/bms_ok_zephyr.c -o bms_ok_adapter_an.o

clang-analyze:
	$(CLANG) -std=c11 -Wall -Wextra -Werror --analyze $(INCLUDES) $(DRIVERS)/bms_ok_zephyr.c

clean:
	rm -f bms_ok_adapter_test bms_ok_adapter_test_asan bms_ok_adapter_test_ubsan bms_ok_adapter_an.o
	@rm -f *.plist
