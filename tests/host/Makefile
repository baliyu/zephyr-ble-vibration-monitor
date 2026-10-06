# PC tests for the board-independent code in src/
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -I../../src
test: test_accel_stats
	./test_accel_stats
test_accel_stats: test_accel_stats.c ../../src/accel_stats.c ../../src/accel_stats.h
	$(CC) $(CFLAGS) -o $@ test_accel_stats.c ../../src/accel_stats.c -lm
clean:
	rm -f test_accel_stats
.PHONY: test clean
