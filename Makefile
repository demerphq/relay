FILES=src/setproctitle.c src/stats.c src/abort.c src/blob.c src/worker.c src/socket_util.c src/config.c src/timer.c \
      src/worker_pool.c src/disk_writer.c src/graphite_worker.c
RELAY=src/relay.c $(FILES)
CLANG=clang
CLANG_FLAGS=-O0 -g3 -fsanitize=thread -fPIE -pie
GCC_FLAGS=-O3
CC=gcc
CFLAGS=-Wall -pthread -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast

all:
	mkdir -p bin
	$(CC) $(CFLAGS) $(GCC_FLAGS) -o bin/relay $(RELAY)

clang:
	mkdir -p bin
	$(CLANG) $(CFLAGS) $(CLANG_FLAGS) -o bin/relay.clang $(RELAY)

clean:
	rm -f bin/relay* test/sock/*

run:
	cd test && ./setup.sh
