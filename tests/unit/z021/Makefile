CC ?= cc
ROOT := ../../..
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -I$(ROOT)/lib/ams_core/include
SRC := support_test.c $(ROOT)/lib/ams_core/adbms/ams_z021_support.c $(ROOT)/lib/ams_core/adbms/ams_adbms_protocol.c
.PHONY: test sanitize analyze clean
test:
	$(CC) $(FLAGS) -O2 $(SRC) -lm -o support_test
	./support_test
sanitize:
	$(CC) $(FLAGS) -O1 -g -fsanitize=address,undefined $(SRC) -lm -o support_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./support_sanitize
analyze:
	$(CC) $(FLAGS) -O2 -fanalyzer -c $(ROOT)/lib/ams_core/adbms/ams_z021_support.c -o /tmp/z021_support.o
clean:
	rm -f support_test support_sanitize
