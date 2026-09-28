CC ?= cc
ROOT := ../../..
FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -I$(ROOT)/lib/ams_core/include
CORE := $(ROOT)/lib/ams_core/measurement/ams_measurement_pipeline.c $(ROOT)/lib/ams_core/measurement/ams_current_window.c $(ROOT)/lib/ams_core/measurement/ams_measurement.c
.PHONY: test adapter sanitize analyze
test:
	$(CC) $(FLAGS) -O2 pipeline_test.c $(CORE) -lm -o pipeline_test
	./pipeline_test
adapter:
	$(CC) $(FLAGS) -Ifake -I$(ROOT)/include -O2 adapter_test.c $(CORE) $(ROOT)/lib/ams_core/current/ams_current_sensor.c $(ROOT)/lib/ams_core/faults/ams_current_fault.c $(ROOT)/lib/ams_core/estimator/ams_segment_consumer.c $(ROOT)/lib/ams_core/estimator/ams_soc_ekf.c $(ROOT)/lib/ams_core/estimator/ams_estimator_lut.c $(ROOT)/drivers/ams/measurement_pipeline_zephyr.c -lm -o adapter_test
	./adapter_test
sanitize:
	$(CC) $(FLAGS) -O1 -g -fsanitize=address,undefined pipeline_test.c $(CORE) -lm -o pipeline_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./pipeline_sanitize
analyze:
	$(CC) $(FLAGS) -O2 -fanalyzer -c $(ROOT)/lib/ams_core/measurement/ams_measurement_pipeline.c -o /tmp/z022_pipeline.o

EST := $(ROOT)/lib/ams_core/estimator/ams_segment_consumer.c $(ROOT)/lib/ams_core/estimator/ams_soc_ekf.c $(ROOT)/lib/ams_core/estimator/ams_estimator_lut.c
consumer:
	$(CC) $(FLAGS) -O2 consumer_test.c $(EST) -lm -o consumer_test
	./consumer_test
consumer-sanitize:
	$(CC) $(FLAGS) -O1 -g -fsanitize=address,undefined consumer_test.c $(EST) -lm -o consumer_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./consumer_sanitize
consumer-analyze:
	$(CC) $(FLAGS) -O2 -fanalyzer -c $(ROOT)/lib/ams_core/estimator/ams_segment_consumer.c -o /tmp/z022_consumer.o
