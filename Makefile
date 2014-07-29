FILES=src/setproctitle.c src/stats.c src/abort.c src/blob.c src/worker.c src/socket_util.c src/config.c src/timer.c \
      src/worker_pool.c src/disk_writer.c src/graphite_worker.c
RELAY=src/relay.c $(FILES)
CLIENT=src/stress_test_client.c $(FILES)
CLANG=clang
CLANG_FLAGS=-O0 -g3 -fsanitize=thread -fPIE -pie
GCC_FLAGS=-O0 -g3
CC=gcc
CFLAGS=-Wall -pthread -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast

all:
	mkdir -p bin
	$(CC) $(CFLAGS) $(GCC_FLAGS) -o bin/client $(CLIENT)
	$(CC) $(CFLAGS) $(GCC_FLAGS) -DUSE_SERVER -o bin/server $(CLIENT)
	$(CC) $(CFLAGS) $(GCC_FLAGS) -o bin/relay $(RELAY)

clang:
	mkdir -p bin/clang
	$(CLANG) $(CFLAGS) $(CLANG_FLAGS) -o bin/clang/client $(CLIENT)
	$(CLANG) $(CFLAGS) $(CLANG_FLAGS) -DUSE_SERVER -o bin/clang/server $(CLIENT)
	$(CLANG) $(CFLAGS) $(CLANG_FLAGS) -o bin/clang/relay $(RELAY)

clean:
	rm -f bin/relay* test/sock/*

run:
	cd test && ./setup.sh
