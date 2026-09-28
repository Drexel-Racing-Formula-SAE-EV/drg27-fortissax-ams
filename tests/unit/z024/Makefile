CC ?= cc
ROOT := ../../..
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -I$(ROOT)/lib/ams_core/include
SCHED := $(ROOT)/lib/ams_core/can/ams_can_tx_scheduler.c
CORE := $(SCHED) $(ROOT)/lib/ams_core/can/ams_can_transport.c $(ROOT)/lib/ams_core/can/ams_can_codec.c
.PHONY: test sanitize analyze
test:
	$(CC) $(FLAGS) -O2 scheduler_test.c $(SCHED) -o scheduler_test
	./scheduler_test
	$(CC) $(FLAGS) -O2 transport_test.c $(CORE) -o transport_test
	./transport_test
sanitize:
	$(CC) $(FLAGS) -O1 -g -fsanitize=address,undefined -fno-pie -no-pie transport_test.c $(CORE) -o transport_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./transport_sanitize
	$(CC) $(FLAGS) -O1 -g -fsanitize=address,undefined -fno-pie -no-pie scheduler_test.c $(SCHED) -o scheduler_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./scheduler_sanitize
analyze:
	$(CC) $(FLAGS) -fanalyzer -c $(ROOT)/lib/ams_core/can/ams_can_transport.c -o transport.o
	$(CC) $(FLAGS) -fanalyzer -c $(ROOT)/lib/ams_core/can/ams_can_codec.c -o codec.o
	$(CC) $(FLAGS) -fanalyzer -c $(SCHED) -o scheduler.o
concurrency:
	$(CC) $(FLAGS) -O2 concurrency_test.c $(CORE) -pthread -o concurrency_test
	./concurrency_test
tsan:
	$(CC) $(FLAGS) -O1 -g -fsanitize=thread -fno-pie -no-pie concurrency_test.c $(CORE) -pthread -o concurrency_tsan
	./concurrency_tsan
