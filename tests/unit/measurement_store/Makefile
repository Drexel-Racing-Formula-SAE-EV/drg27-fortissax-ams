AMS_CORE ?= ../../../lib/ams_core
CC ?= gcc
CLANG ?= clang
BASE := -std=c11 -Wall -Wextra -Werror -pedantic -I$(AMS_CORE)/include -pthread
SRC := measurement_store_test.c $(AMS_CORE)/measurement/ams_measurement.c

.PHONY: all test asan ubsan analyze clang-analyze clean
all: test

test:
	$(CC) $(BASE) -O2 $(SRC) -o measurement_store_test
	./measurement_store_test

asan:
	$(CC) $(BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(SRC) -o measurement_store_test_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./measurement_store_test_asan

ubsan:
	$(CC) $(BASE) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all $(SRC) -o measurement_store_test_ubsan
	./measurement_store_test_ubsan

analyze:
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer -I$(AMS_CORE)/include -c $(AMS_CORE)/measurement/ams_measurement.c -o measurement_an.o

clang-analyze:
	$(CLANG) -std=c11 -Wall -Wextra -Werror --analyze -I$(AMS_CORE)/include $(AMS_CORE)/measurement/ams_measurement.c

clean:
	rm -f measurement_store_test measurement_store_test_asan measurement_store_test_ubsan measurement_an.o
	@rm -f *.plist
