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

# -Wconversion is still too noisy
# -Wcast-align is useless with gcc on x86: it never warns.  Use it with clang.
WARN_FLAGS=-Wall -Wextra -Wunused -Wshadow -Wpointer-arith -Wcast-qual -Wcast-align

# Platform-specific flags.

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

ifeq ($(uname_S),Linux)
  OS_FLAGS=-D_BSD_SOURCE -D_GNU_SOURCE -D_POSIX_SOURCE -DHAVE_MALLINFO
endif

ifeq ($(uname_S),Darwin)
  OS_FLAGS=-D_BSD_SOURCE -D_GNU_SOURCE
endif

# Flags common to all compilers.
CFLAGS=$(OPT_FLAGS) $(SAN_FLAGS) $(WARN_FLAGS) $(OS_FLAGS) -pthread -std=c99 -fno-omit-frame-pointer

GCC_FLAGS=$(CFLAGS)
CLANG_FLAGS=$(CFLAGS)

LIBS = -lm

SRC=src/setproctitle.c src/stats.c src/control.c src/blob.c src/socket_worker.c src/socket_util.c src/string_util.c src/config.c \
	src/timer.c src/socket_worker_pool.c src/disk_writer.c src/graphite_worker.c src/relay.c src/global.c src/daemonize.c src/worker_util.c

# The executable names.
RELAY=event-relay
RELAY_CLANG=$(RELAY).clang

all:	gcc clang

gcc:
	mkdir -p bin
	$(GCC) $(CFLAGS) -o bin/$(RELAY) $(SRC) $(LIBS)

debug:	clean
	make gcc OPT_FLAGS="-O0 -g"
	make clang OPT_FLAGS="-O0 -g"

gcc.asan:
	mkdir -p bin
	make gcc OPT_FLAGS=-g SAN_FLAGS=$(ASAN_FLAGS)

gcc.tsan:
	mkdir -p bin
	make gcc OPT_FLAGS=-g SAN_FLAGS="$(TSAN_FLAGS) -pie -fPIC"

gcc.ubsan:
	mkdir -p bin
	make gcc OPT_FLAGS=-g SAN_FLAGS=$(UBSAN_FLAGS)

clang:
	mkdir -p bin
	$(CLANG) $(CFLAGS) -o bin/$(RELAY_CLANG) $(SRC) $(LIBS)

clang.asan:
	mkdir -p bin
	make clang OPT_FLAGS=-g SAN_FLAGS=$(ASAN_FLAGS)

clang.msan:
	mkdir -p bin
	make clang OPT_FLAGS=-g SAN_FLAGS=$(MSAN_FLAGS)

clang.tsan:
	mkdir -p bin
	make clang OPT_FLAGS=-g SAN_FLAGS=$(TSAN_FLAGS)

indent:
	sh indent.sh src/*.[hc]

clean:
	rm -rf bin/$(basename $(RELAY))*.dSYM
	rm -f bin/$(basename $(RELAY))* test/sock/*
