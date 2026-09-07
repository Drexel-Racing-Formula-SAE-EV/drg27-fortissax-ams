AMS_CORE ?= ../../../lib/ams_core
CC ?= gcc

COMMON_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
	-I$(AMS_CORE)/include

SOP_SRC := \
	$(AMS_CORE)/sop/ams_sop.c \
	$(AMS_CORE)/estimator/ams_estimator_lut.c

FUSE_SRC := \
	$(AMS_CORE)/sop/ams_fuse_observer.c \
	$(AMS_CORE)/sop/ams_sop.c \
	$(AMS_CORE)/estimator/ams_estimator_lut.c

SOH_SRC := $(AMS_CORE)/soh/ams_soh.c

.PHONY: all test metamorphic fuse soh asan clean

all: test

test: metamorphic fuse soh

metamorphic:
	$(CC) $(COMMON_FLAGS) sop_metamorphic_oracle.c $(SOP_SRC) -lm -o sop_metamorphic_oracle
	./sop_metamorphic_oracle 20000

fuse:
	$(CC) $(COMMON_FLAGS) \
		-Wconversion -Wsign-conversion -Wshadow -Wdouble-promotion \
		fuse_oracle_test.c fuse_reference_oracle.c $(FUSE_SRC) \
		-lm -o fuse_oracle_test
	./fuse_oracle_test

soh:
	$(CC) $(COMMON_FLAGS) soh_core_test.c $(SOH_SRC) -lm -o soh_core_test
	./soh_core_test

asan:
	$(CC) $(COMMON_FLAGS) -O1 -g -fsanitize=address,undefined \
		sop_metamorphic_oracle.c $(SOP_SRC) -lm -o sop_metamorphic_oracle_asan
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ./sop_metamorphic_oracle_asan 500
	$(CC) $(COMMON_FLAGS) -O1 -g -fsanitize=address,undefined \
		fuse_oracle_test.c fuse_reference_oracle.c $(FUSE_SRC) \
		-lm -o fuse_oracle_test_asan
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ./fuse_oracle_test_asan
	$(CC) $(COMMON_FLAGS) -O1 -g -fsanitize=address,undefined \
		soh_core_test.c $(SOH_SRC) -lm -o soh_core_test_asan
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ./soh_core_test_asan

clean:
	rm -f \
		sop_metamorphic_oracle sop_metamorphic_oracle_asan \
		fuse_oracle_test fuse_oracle_test_asan \
		soh_core_test soh_core_test_asan
