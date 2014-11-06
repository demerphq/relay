GCC=gcc
CLANG=clang

# Sanitizer flags

ASAN_FLAGS=-fsanitize=address
TSAN_FLAGS=-fsanitize=thread

# msan only works with clang, not gcc 4.9.1
# -fsanitize-memory-track-origins=2 requires newer clang (3.6?)
MSAN_FLAGS=-fsanitize=memory -fsanitize-memory-track-origins

# ubsan only works with gcc 4.9.1, not clang 3.4.2
UBSAN_FLAGS=-fsanitize=undefined
# UBSAN_FLAGS=-fsanitize=undefined -fno-sanitize-recover
# -fno-sanitize-recover would cause ubsan violations
# to be fatal but it doesn't work with gcc 4.9.1.
# The violations are just dumped to stderr.
# (Silly since asan and tsan violations are deadly.)

# Generic flags

OPT_FLAGS=-O3
DBG_FLAGS=-g

# -Wconversion is still too noisy
# -Wcast-align is useless with gcc on x86: it never warns.  Use it with clang.
WARN_FLAGS=-Wall -Wextra -Wunused -Wshadow -Wpointer-arith -Wcast-qual -Wcast-align

# Linux:
OS_FLAGS=-D_BSD_SOURCE -D_GNU_SOURCE -D_POSIX_SOURCE -DHAVE_MALLINFO

# OS X
# OS_FLAGS=-D_BSD_SOURCE -D_GNU_SOURCE

# Flags common to all compilers.
CFLAGS=$(OPT_FLAGS) $(DBG_FLAGS) $(WARN_FLAGS) $(OS_FLAGS) -pthread -std=c99 -fno-omit-frame-pointer

GCC_FLAGS=$(CFLAGS)
CLANG_FLAGS=$(CFLAGS)

SRC=src/setproctitle.c src/stats.c src/control.c src/blob.c src/socket_worker.c src/socket_util.c src/string_util.c src/config.c \
	src/timer.c src/socket_worker_pool.c src/disk_writer.c src/graphite_worker.c src/relay.c src/global.c src/daemonize.c src/worker_util.c

# The executable names.
RELAY=event-relay
RELAY_CLANG=$(RELAY).clang

all:	gcc clang

gcc:
	mkdir -p bin
	$(GCC) $(CFLAGS) -o bin/$(RELAY) $(SRC)

gcc.asan:
	mkdir -p bin
	$(GCC) $(CFLAGS) $(ASAN_FLAGS) -o bin/$(RELAY) $(SRC)

gcc.tsan:
	mkdir -p bin
	$(GCC) $(CFLAGS) $(TSAN_FLAGS) -pie -fPIC -o bin/$(RELAY) $(SRC)

gcc.ubsan:
	mkdir -p bin
	$(GCC) $(CFLAGS) $(UBSAN_FLAGS) -o bin/$(RELAY) $(SRC)

clang:
	mkdir -p bin
	$(CLANG) $(CFLAGS) -o bin/$(RELAY_CLANG) $(SRC)

clang.asan:
	mkdir -p bin
	$(CLANG) $(CFLAGS) $(ASAN_FLAGS) -o bin/$(RELAY_CLANG) $(SRC)

clang.msan:
	mkdir -p bin
	$(CLANG) $(CFLAGS) $(MSAN_FLAGS) -o bin/$(RELAY_CLANG) $(SRC)

clang.tsan:
	mkdir -p bin
	$(CLANG) $(CFLAGS) $(TSAN_FLAGS) -o bin/$(RELAY_CLANG) $(SRC)

indent:
	indent -kr --line-length 120 src/*.[hc]

clean:
	rm -rf bin/$(basename $(RELAY))*.dSYM
	rm -f bin/$(basename $(RELAY))* test/sock/*
run:
	cd test && ./setup.sh
