AMS_CORE ?= ../../../lib/ams_core
CC ?= gcc
CLANG ?= clang
BASE := -std=c11 -Wall -Wextra -Werror -pedantic -I$(AMS_CORE)/include
SRC := current_window_test.c $(AMS_CORE)/measurement/ams_current_window.c

.PHONY: all test asan ubsan analyze clang-analyze clean
all: test

test:
	$(CC) $(BASE) -O2 $(SRC) -lm -o current_window_test
	./current_window_test

asan:
	$(CC) $(BASE) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(SRC) -lm -o current_window_test_asan
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./current_window_test_asan

ubsan:
	$(CC) $(BASE) -O1 -g -fsanitize=undefined -fno-sanitize-recover=all $(SRC) -lm -o current_window_test_ubsan
	./current_window_test_ubsan

analyze:
	$(CC) -std=c11 -Wall -Wextra -Werror -fanalyzer -I$(AMS_CORE)/include -c $(AMS_CORE)/measurement/ams_current_window.c -o current_window_an.o

clang-analyze:
	$(CLANG) -std=c11 -Wall -Wextra -Werror --analyze -I$(AMS_CORE)/include $(AMS_CORE)/measurement/ams_current_window.c

clean:
	rm -f current_window_test current_window_test_asan current_window_test_ubsan current_window_an.o
	@rm -f *.plist
