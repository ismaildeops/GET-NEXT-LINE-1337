# get_next_line tester - thin wrapper around run_tests.sh
# Put this folder inside your project (next to get_next_line.c) or set GNL_DIR.
GNL_DIR ?= ..
export GNL_DIR

all: test

test:            ## everything: static checks + plain + ASan/UBSan (+ valgrind if installed)
	./run_tests.sh

quick:           ## 4 BUFFER_SIZE values, no valgrind (about 1 minute)
	BUFFER_SIZES="1 4 42 1024" MODES="plain asan" ./run_tests.sh

asan:            ## AddressSanitizer + UBSan only
	MODES="asan" ./run_tests.sh

valgrind:        ## valgrind only (slow, light tests)
	MODES="valgrind" ./run_tests.sh

heavy:           ## adds the 1 MB-line tests (large BUFFER_SIZE only)
	LEVEL=2 BUFFER_SIZES="1024 4096 10000000" MODES="plain asan" ./run_tests.sh

one:             ## make one BS=5 ID=B07   (one test, one BUFFER_SIZE, plain build)
	./run_tests.sh --bs $(BS) --modes plain --only $(ID)

helpers:         ## your helper functions (template: edit src/helpers_test.c first)
	cc -g -O0 -Wall -Wextra -fsanitize=address,undefined -DBUFFER_SIZE=42 -I$(GNL_DIR) \
		src/helpers_test.c $(GNL_DIR)/get_next_line_utils.c -o build_helpers && ./build_helpers; \
		rc=$$?; rm -f build_helpers; exit $$rc

clean:
	rm -rf build results build_helpers

.PHONY: all test quick asan valgrind heavy one helpers clean
