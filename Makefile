FILES=src/setproctitle.c src/blob.c src/worker.c src/util.c
RELAY=src/relay.c $(FILES)
CLIENT=src/stress_test_client.c $(FILES)
CLANG=clang
CLANG_FLAGS=-fsanitize=thread -fPIE -pie
CC=gcc
CFLAGS=-O0 -g3 -Wall -pthread -DMAX_WORKERS=2 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast

all:
	mkdir -p bin
	$(CC) $(CFLAGS) -o bin/client $(CLIENT)
	$(CC) $(CFLAGS) -DUSE_SERVER -o bin/server $(CLIENT)
	$(CC) $(CFLAGS) -o bin/relay $(RELAY)

clang:
	mkdir -p bin/clang
	$(CLANG) $(CFLAGS) $(CLANG_FLAGS) -o bin/clang/client $(CLIENT)
	$(CLANG) $(CFLAGS) $(CLANG_FLAGS) -DUSE_SERVER -o bin/clang/server $(CLIENT)
	$(CLANG) $(CFLAGS) $(CLANG_FLAGS) -o bin/clang/relay $(RELAY)

clean:
	rm -f bin/relay* test/sock/*
	
run:
	cd test && ./setup.sh
