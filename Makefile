RELAY=src/setproctitle.c src/stats.c src/control.c src/blob.c src/worker.c src/socket_util.c src/string_util.c src/config.c src/timer.c src/worker_pool.c src/disk_writer.c src/graphite_worker.c src/relay.c
CLANG=clang
CLANG_FLAGS=-O0 -g3 -fno-omit-frame-pointer
CLANG_ASAN_FLAGS=-fsanitize=address
# -fsanitize-memory-track-origins=2 requires newer clang (3.6?)
CLANG_MSAN_FLAGS=-fsanitize=memory -fsanitize-memory-track-origins
CLANG_TSAN_FLAGS=-fsanitize=thread
GCC_FLAGS=-O3
CC=gcc
CFLAGS=-Wall -Wextra -pthread -std=c99 -D_BSD_SOURCE -D_POSIX_SOURCE

all:
	mkdir -p bin
	$(CC) $(CFLAGS) $(GCC_FLAGS) -o bin/relay $(RELAY)

clang:
	mkdir -p bin
	$(CLANG) $(CFLAGS) $(CLANG_FLAGS) -o bin/relay.clang $(RELAY)

clang.asan:
	mkdir -p bin
	$(CLANG) $(CFLAGS) $(CLANG_FLAGS) $(CLANG_ASAN_FLAGS) -o bin/relay.clang $(RELAY)

clang.msan:
	mkdir -p bin
	$(CLANG) $(CFLAGS) $(CLANG_FLAGS) $(CLANG_MSAN_FLAGS) -o bin/relay.clang $(RELAY)

clang.tsan:
	mkdir -p bin
	$(CLANG) $(CFLAGS) $(CLANG_FLAGS) $(CLANG_TSAN_FLAGS) -o bin/relay.clang $(RELAY)

indent:
	indent -kr --line-length 120 src/*.[hc]

clean:
	rm -f bin/relay* test/sock/*

run:
	cd test && ./setup.sh
