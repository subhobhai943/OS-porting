CC ?= gcc
CFLAGS ?= -std=gnu11 -Wall -Wextra -O2 -fno-builtin -Wno-stringop-truncation

INCLUDES := -I../kernel/include -I../lib/libc -Iframework

FRAMEWORK_SRCS := framework/test.c framework/host_io.c
STRING_TEST_SRCS := unit/test_runner.c unit/test_string.c ../lib/libc/string.c

.PHONY: all unit-string clean

all: unit-string

build:
	@mkdir -p build

unit-string: build
	$(CC) $(CFLAGS) $(INCLUDES) $(FRAMEWORK_SRCS) $(STRING_TEST_SRCS) -o build/test_string
	./build/test_string

clean:
	rm -rf build
