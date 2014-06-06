FILES=src/blob.c src/worker.c src/util.c
RELAY=src/relay.c $(FILES)
CLIENT=src/stress_test_client.c $(FILES)
CC=clang
CFLAGS=-O3 -Wall -pthread -g -DMAX_WORKERS=2 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
all:
	mkdir -p bin
	$(CC) $(CFLAGS) -o bin/client $(CLIENT)
	$(CC) $(CFLAGS) -DUSE_SERVER -o bin/server $(CLIENT)
	$(CC) $(CFLAGS) -o bin/relay $(RELAY)

clean:
	rm -f bin/relay* test/sock/*
	
run:
	cd test && ./setup.sh
