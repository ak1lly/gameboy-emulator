CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2

.PHONY: all test clean
all: test_cpu

test_cpu: cpu.c cpu.h test_cpu.c
	$(CC) $(CFLAGS) cpu.c test_cpu.c -o $@

test: test_cpu
	./test_cpu

clean:
	rm -f test_cpu
